# Nineteen — audit de sécurité du code d'origine

**Périmètre** : le jeu et son site tels qu'ils tournaient en version 1.1.2, mai 2020.
Client C (`legacy/main.c`, `legacy/include/`, `legacy/room/`) et backend PHP
(`legacy/site-web/`). Revue de code statique intégrale, sans test d'intrusion sur
l'infrastructure.

**Méthode** : lecture ligne à ligne, chaque constat rattaché à un emplacement précis et,
quand c'était possible, vérifié par le calcul plutôt qu'à l'œil. Trois hypothèses initiales
ont d'ailleurs été **écartées** après vérification — elles figurent en fin de document,
parce qu'un audit qui ne publie que ses succès n'est pas vérifiable.

**Verdict** : non déployable en l'état. 22 constats, dont 7 critiques.

---

## Ce qui était bien vu

Il faut commencer par là, parce que c'est vrai et que ça oriente la lecture du reste.

Ce code n'est pas celui de quelqu'un qui ignorait la sécurité. On y trouve, en 2020, dans un
projet d'études :

- **un jeton anti-rejeu** sur chaque requête au serveur, avec récupération de l'heure
  serveur pour éviter la dérive d'horloge du client ;
- **une protection mémoire du score** (`include/hashage.c`) : le score est conservé en clair
  et sous forme dérivée, avec des clés régénérées à chaque écriture, et toute incohérence
  entre les deux déclenche un bannissement ;
- **l'échappement des tickets** avant insertion en base ;
- **une validation du format d'e-mail** à l'inscription ;
- **un tampon d'erreur** et une boîte de dialogue native pour ne pas mourir en silence.

L'intention défensive est là, et elle est même inhabituelle à ce niveau. Ce qui manque, c'est
le **modèle de menace** : chaque protection défend contre un attaquant plus faible que celui
qu'elle affronte réellement. Le jeton anti-rejeu suppose que l'attaquant ne lit pas le
binaire ; la protection mémoire suppose qu'il ne contrôle pas le processus ; l'échappement
des tickets suppose que c'est le seul endroit où entrent des données. Aucune de ces trois
hypothèses ne tient.

C'est un écart d'exécution, pas de compréhension.

---

## Synthèse

| Sévérité | Nb | Identifiants |
|---|---|---|
| 🔴 Critique | 7 | NIN-01 · NIN-02 · NIN-03 · NIN-04 · NIN-05 · NIN-06 · NIN-22 |
| 🟠 Élevé | 6 | NIN-07 · NIN-08 · NIN-09 · NIN-10 · NIN-11 · NIN-12 |
| 🟡 Moyen | 5 | NIN-13 · NIN-14 · NIN-15 · NIN-16 · NIN-17 |
| 🟢 Faible | 4 | NIN-18 · NIN-19 · NIN-20 · NIN-21 |

---

## Critiques

### NIN-01 — Injection SQL sur toute la surface du site
**CWE-89** · `connect.php:72`, `coins.php:17,56`, `index.php:15,22,37,42`,
`inscription.php:39,52,56`, `download.php:57`, `updateYourScore.php`

Toutes les requêtes concatènent les entrées utilisateur. Le cas le plus direct :

```php
// connect.php:72
$result = $link->query("SELECT userId FROM nineteen_players
                        WHERE username = '$username' AND password = '$password'");
```

**Exploitation.** Saisir `admin' -- ` comme identifiant met le reste de la clause en
commentaire. La requête devient `WHERE username = 'admin' -- ' AND password = '…'`, le mot
de passe n'est plus évalué, et le serveur émet une session pour `admin`. Aucun outil n'est
nécessaire : le formulaire de connexion suffit.

`index.php:22,42` injecte en plus dans un contexte numérique
(`WHERE score > $scoreRecherche[0]`), ce qui ouvre `UNION SELECT` et donc la lecture de
n'importe quelle table, dont `nineteen_players`.

**Correction V15.** Requêtes préparées partout (`server/internal/store/`). Vérifié :
la tentative ci-dessus renvoie « identifiant ou mot de passe incorrect ».

---

### NIN-02 — Le jeton « secure » ne protège rien
**CWE-330, CWE-798** · `include/libWeb.c:216-236`, `site-web/include/fonction.php:37`

```c
// libWeb.c:231
sprintf(temp, "%d-%02d-%02d JDlaliljasnc329832 %02d 0 %02d D(ancIjaa) %d",
        year, mon, day, hour, min, sec);
md5Hash(temp, secure);
```

Le jeton est le MD5 d'une chaîne composée de la date et de deux constantes littérales,
`JDlaliljasnc329832` et `D(ancIjaa)`. Ces constantes sont dans le binaire distribué à tous
les joueurs — `strings Nineteen` les affiche. La date vient du serveur, par
`include/timestamp.php`, un point d'entrée public.

**Exploitation.** L'attaquant appelle `timestamp.php`, récupère l'heure, recompose la même
chaîne, calcule le même MD5. Il produit alors autant de jetons valides qu'il veut, sans
jamais lancer le jeu. Le mécanisme coûte un aller-retour réseau et n'oppose rien.

Le problème n'est pas MD5 : c'est qu'un secret partagé avec tous les clients n'est pas un
secret. Il n'y a pas de correctif possible à périmètre constant — d'où NIN-06.

---

### NIN-03 — Vérification TLS désactivée sur toutes les requêtes
**CWE-295** · `include/libWeb.c:169-170`

```c
curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0);
curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
```

Les huit URL du jeu sont en `https://`, ce qui donne l'apparence du chiffrement. Ces deux
lignes le vident de son sens : n'importe quel certificat est accepté, y compris auto-signé.

**Exploitation.** Sur un réseau partagé — un Wi-Fi d'école, précisément le contexte de ce
projet — un intercepteur présente son propre certificat. Il lit alors en clair l'identifiant
et le mot de passe envoyés par `construire_requete` (`libWeb.c:294`), ainsi que la clé de
session, et peut réécrire les réponses.

C'est le constat le plus grave du lot, parce qu'il compromet des mots de passe que les
joueurs réutilisent probablement ailleurs.

**Correction V15.** Options supprimées, bundle CA fourni, épinglage de la clé publique
prévu.

---

### NIN-04 — Déconnexion de tous les joueurs, sans authentification
**CWE-306** · `site-web/disconnectAll.php:3`

```php
if ($result = $link->query("DELETE FROM nineteen_session WHERE 1")) {
```

Neuf lignes, aucun contrôle d'accès.

**Exploitation.** Visiter l'URL déconnecte tous les joueurs du jeu. Une boucle `curl` rend
le service inutilisable de façon permanente et sans coût.

**Correction V15.** L'action existe toujours — elle est légitime — mais exige une session
valide et ne révoque que les sessions du compte demandeur
(`server/internal/store/store.go`, `RevokePlayerSessions`).

---

### NIN-05 — Point d'entrée de débogage divulguant des jetons valides
**CWE-215** · `site-web/connect.php` (branche GET), `include/fonction.php:37`

Une branche de débogage laissée en production imprime les jetons `secure` calculés pour un
intervalle de secondes. Un attaquant qui n'aurait pas su reconstruire l'algorithme de NIN-02
les obtient directement.

**Correction V15.** Aucun point d'entrée de débogage. Le détail des erreurs part dans le
journal serveur, jamais dans la réponse.

---

### NIN-06 — Le serveur croit le score que le client lui annonce
**CWE-602, CWE-807** · `site-web/updateYourScore.php`, `include/hashage.c`, `include/libWeb.c:314`

C'est la faille de conception dont les autres découlent.

```c
// libWeb.c:314
sprintf(*dest, "gameID=%s&score=%s&key=%s&secure=%s", gameID, score, key, secure);
```

Le score est une valeur transmise par le client, que le serveur enregistre après avoir
vérifié un jeton que le client peut fabriquer (NIN-02).

**Exploitation.** Une requête HTTP construite à la main suffit : pas besoin de toucher au
jeu, ni de comprendre son code.

**Et la protection mémoire ?** `include/hashage.c` mérite un examen sérieux, parce que
l'idée est bonne et que c'est le seul endroit du projet où l'auteur a écrit une contre-mesure
originale. Elle échoue pour quatre raisons cumulatives :

```c
// hashage.c:27-30
hash = value * keys[0];
hash = hash - keys[1];
hash = hash * keys[2];
hash = hash / keys[3];
```

1. **La fonction est affine et inversible.** Deux couples (valeur, empreinte) suffisent à
   remonter aux clés ; un seul suffit si l'on connaît déjà trois clés.
2. **Les clés sont dans le même espace mémoire que la valeur qu'elles protègent**, souvent
   dans la même trame de pile. Qui peut modifier le score peut modifier les clés.
3. **`rand()` n'est jamais initialisé.** Aucun appel à `srand` dans tout le projet
   (vérifié : `grep -rn srand legacy/` ne renvoie rien). La suite de clés est donc
   *identique à chaque lancement du jeu*, sur une même machine. L'aléa recherché n'existe pas.
4. **La division entière perd de l'information** : la transformation n'est pas injective,
   plusieurs scores partagent une empreinte.

Et surtout : le test lui-même (`changeProtectedVar`, `hashage.c:67`) est une instruction de
comparaison dans un processus que l'attaquant contrôle. La remplacer par un saut
inconditionnel désactive le tout.

**Ce n'était pas inutile pour autant.** Contre un joueur qui cherche l'adresse du score dans
un éditeur mémoire et la modifie à la volée, la protection fonctionne : la valeur et
l'empreinte cessent de concorder et la partie est invalidée. Elle arrête la triche
opportuniste. Elle n'arrête pas quelqu'un qui lit le code — et le code était distribué.

**Correction V15.** L'autorité change de camp. Le serveur ouvre la partie, tire la graine et
un secret propre à celle-ci ; le client renvoie un journal d'événements scellé par HMAC-SHA256 ;
le serveur **recalcule** le score et rejette l'implausible — débit d'événements, désordre
temporel, durée annoncée supérieure au temps réellement écoulé, événements inconnus,
plafond par jeu. Une partie ne peut être soumise qu'une fois.
Voir `server/internal/runs/`, et `server/internal/runs/runs_test.go` pour les 16 scénarios
d'attaque couverts.

La protection mémoire est conservée côté client, modernisée, et **documentée comme
ralentisseur** — ce qu'elle est réellement.

### NIN-22 — Débordement de tas déclenchable par le réseau, sur la clé de session
**CWE-787, CWE-120, CWE-1284** · `include/libWeb.c:397-399`, `main.c:94`, `main.c` (allocation de `token`)

Celui-ci est le plus sérieux du lot avec NIN-03, et il se lit en trois morceaux disséminés
dans deux fichiers — c'est pourquoi il avait échappé à une première passe.

```c
// libWeb.c:397-399
if ( strlen(response) >= 255 )
{
    strcpy(connectStruct->key, response);
```

**Le garde est inversé.** La condition n'est pas une limite haute mais une limite *basse* :
la copie n'a lieu **que si** la réponse fait au moins 255 octets, et rien ne borne sa taille
par le haut. L'intention était visiblement « une clé de session valide est longue, donc si la
réponse est longue c'est une clé » ; le test de plausibilité a été écrit à la place du test de
sécurité.

**La destination.** `connectStruct->key` reçoit `tokenCpy`, alloué dans `main.c` par
`_malloc(&tokenCpy, sizeof(char), SIZE_SESSION + 1, …)` avec `SIZE_SESSION` valant 256
(`main.c:94`). L'intention est donc un tampon de 257 octets.

**Exploitation.** La réponse vient du réseau, et NIN-03 a désactivé la vérification TLS. Un
intercepteur sur le réseau répond à la requête de connexion par 4 000 octets au lieu d'une
clé. Le `strcpy` les recopie intégralement dans un tampon de tas prévu pour 257. On obtient
une écriture hors bornes de longueur et de contenu contrôlés par l'attaquant, dans le tas du
client — le primitif de départ classique d'une exécution de code.

**Et une ironie.** Le tampon n'est pas réellement de 257 octets. À cause du défaut de
`_malloc` (NIN-10), l'allocation vaut `sizeof(int) * 257`, soit **1 028 octets**. Les deux
bugs se compensent partiellement : une réponse entre 257 et 1 028 octets, qui aurait dû
déborder, tombe dans la sur-allocation accidentelle. Il faut dépasser 1 028 octets pour
sortir du bloc — ce qui ne change rien à la conclusion, puisque l'attaquant choisit la
longueur.

C'est le seul endroit du projet où deux défauts distincts interagissent, et cela illustre
pourquoi on ne corrige pas une classe de bugs « quand on aura le temps » : la marge qui
sauve accidentellement ce code vient d'une autre erreur.

**Correction V15.** Le client ne recopie plus de réponse dans un tampon fixe. Les lectures
réseau vont dans une arène dimensionnée sur la taille annoncée, la clé de session est
validée en longueur et en jeu de caractères avant usage, et la vérification TLS est
rétablie — ce qui retire le vecteur d'entrée.

---

## Élevés

### NIN-07 — Mots de passe en MD5 non salé, et politique inversée
**CWE-916, CWE-521** · `connect.php:33`, `inscription.php:14,20,51`

```php
$passMD5 = MD5($password);                          // inscription.php:51
if (strlen($password) < 4) { /* refus */ }          // inscription.php:14
if (!preg_match("/^[a-zA-Z0-9]*$/", $password))     // inscription.php:20
```

MD5 non salé se casse par table arc-en-ciel, pas par force brute : le temps de calcul est
sans objet. Sans sel, deux joueurs de même mot de passe ont la même empreinte.

La politique aggrave le tout dans les deux sens : quatre caractères sont acceptés, et les
caractères non alphanumériques sont **refusés** — donc `Correct-Cheval-Batterie-Agrafe` est
rejeté au profit de `abcd`. Restreindre le jeu de caractères ne fait que réduire l'espace de
recherche de l'attaquant.

**Correction V15.** Argon2id (64 Mio, 2 passes), 12 caractères minimum, aucune restriction
de caractères, refus des mots de passe trop répandus, comparaison à temps constant.

---

### NIN-08 — Injection de commande par les boîtes de dialogue
**CWE-78** · `include/communFunctions.c:132,138,144`

```c
sprintf(commande, "osascript -e 'tell application (path to frontmost application as text) "
                  "to display dialog \"%s\" buttons {\"QUITTER\"} with icon stop'", message);
system(commande);
```

`message` est interpolé dans une commande shell sans échappement, sur les trois
plateformes. Un message contenant un guillemet double sort de la chaîne AppleScript ; un
`'` sort de la chaîne shell.

**Exploitation.** Le vecteur dépend de l'origine de `message`. Plusieurs appels affichent
du texte issu de réponses serveur — et NIN-03 permet à un intercepteur réseau de fabriquer
ces réponses. La chaîne d'attaque est donc : réseau partagé → interception TLS → réponse
forgée → exécution de commande arbitraire sur la machine du joueur.

**Correction V15.** Plus aucun appel à `system()`. Les messages passent par
`SDL_ShowSimpleMessageBox`, qui prend une chaîne et non une ligne de commande
(`engine/core/ns_log.c`).

Au passage, `communFunctions.c:40-42` collecte modèle de processeur, carte graphique et
version d'OS par `system_profiler` et `wmic`, les écrit dans `/tmp/Nineteen.tmp` puis les
transmet au serveur. Ce n'est pas une faille, mais c'est une collecte de données matérielles
non annoncée à l'utilisateur.

---

### NIN-09 — Dépassement de pile d'un octet à chaque requête authentifiée
**CWE-787, CWE-193** · `include/libWeb.c:125-135`, appelée depuis les lignes 260, 280, 301, 320

```c
#define MD5_SIZE 32                     // libWeb.c:38
char secure[MD5_SIZE];                  // libWeb.c:260 — 32 octets

void md5Hash(char *string, char *hash) {
    for (i = 0; i < MD5_DIGEST_LENGTH; i++) {   // 16 itérations
        sprintf(hash + 2*i, "%02x", md5[i]);
    }
}
```

Seize itérations écrivent 32 caractères hexadécimaux, ce qui remplit exactement le tampon.
Mais `sprintf` ajoute un zéro terminal : à la dernière itération, `i = 15`, l'écriture
commence à `hash + 30` et porte sur les octets 30, 31 **et 32**. Le tampon n'en a que 32,
d'indices 0 à 31.

Un octet nul est donc écrit juste après un tableau de pile, à chaque connexion, chaque envoi
de score et chaque consultation du classement — soit quatre sites d'appel.

**Impact.** Ce qui suit `secure[]` dans la trame de pile dépend du compilateur et des
options ; selon le cas, l'octet tombe dans du remplissage (inoffensif), écrase le premier
octet d'une variable voisine, ou touche un canari de pile et fait terminer le programme.
Le comportement est indéfini, donc il peut changer d'une recompilation à l'autre — c'est ce
qui rend ce genre de bug pénible à diagnostiquer.

Le tampon aurait dû faire 33 octets. C'est l'erreur classique du « une chaîne de N
caractères tient dans N octets ».

Ce constat est le seul du lot qu'aucune relecture rapide n'aurait trouvé : il fallait
compter. Il est donc **prouvé plutôt qu'affirmé**. La reproduction minimale, extraite telle
quelle du fichier d'origine, est dans `docs/audit/nin09-repro.c` :

```
$ gcc -fsanitize=address -g -O1 -o repro docs/audit/nin09-repro.c && ./repro

==30149==ERROR: AddressSanitizer: stack-buffer-overflow
WRITE of size 3 at 0x7f4182400060
    #3 md5Hash_original /tmp/repro.c:12
    #4 main /tmp/repro.c:22

This frame has 2 object(s):
    [32, 48) 'digest'
    [64, 96) 'secure' <== Memory access at offset 96 overflows this variable
```

Le tampon occupe les octets 64 à 95 ; l'écriture porte sur l'octet 96. Un octet exactement,
comme le calcul l'annonçait.

**Correction V15.** Plus de jeton MD5 du tout (NIN-02/NIN-06). Le principe général est
appliqué au moteur : les tampons sont dimensionnés à `N+1`, `SDL_snprintf` remplace
`sprintf`, et la CI compile sous ASan et UBSan — qui auraient signalé cette écriture
immédiatement.

---

### NIN-10 — Allocation dimensionnée par `sizeof` d'un paramètre
**CWE-131** · `include/communFunctions.c:110-112`

```c
int _malloc(void **ptr, int type, int size, /* … */)
{
    *ptr = malloc(sizeof(type) * size);
```

`type` est un paramètre de type `int`. `sizeof(type)` vaut donc **toujours** `sizeof(int)`,
soit 4 — quelle que soit la valeur transmise par l'appelant. L'intention était clairement
`malloc(type * size)`.

**Impact réel, vérifié.** Il faut être précis ici : les huit sites d'appel du projet
(`room/room.c:1064,1297,2771`, `main.c:365,375,385,395,797`) passent tous un type de 4 octets
ou moins — `sizeof(char)` ou `sizeof(GLuint)`. La taille calculée est donc *égale ou
supérieure* à celle voulue, et **aucun débordement ne se produit dans le code tel qu'il est
écrit**. Pour `sizeof(char)`, le programme réserve quatre fois trop.

C'est donc une mine, pas un trou : le premier appel avec une structure — le cas d'usage
naturel de ce genre d'utilitaire — sous-alloue silencieusement et provoque un débordement de
tas à l'écriture. Classé élevé pour cette raison, et non critique.

**Correction V15.** Les macros `NS_ARENA_NEW` / `NS_ARENA_ARRAY` dérivent taille *et*
alignement du type lui-même (`engine/core/ns_core.h`) : l'erreur devient inexprimable. Un
test la couvre (`tests/test_core.c`, `test_arena`).

---

### NIN-11 — XSS réfléchi et stocké
**CWE-79** · `inscription.php`, `index.php:62-90`, `download.php:323`

Les valeurs issues de la base et des formulaires sont écrites dans les pages sans
échappement, et le site n'a aucun en-tête `Content-Security-Policy`.

**Exploitation.** Le pseudo est le vecteur le plus propre : il est choisi par l'attaquant à
l'inscription, stocké, puis affiché à tous les visiteurs du classement. Un pseudo contenant
une balise de script s'exécute donc dans le navigateur de chaque joueur qui consulte le
classement, avec accès au cookie de session — celui-ci n'étant pas `HttpOnly`.

**Correction V15.** Le front n'utilise jamais `innerHTML` : tout passe par `textContent`
(`server/internal/web/assets/app.js`). CSP stricte sans `unsafe-inline`, cookie de session
`HttpOnly`.

---

### NIN-12 — Clés de session non cryptographiques
**CWE-338** · `site-web/connect.php`, `include/hashage.c:51`

Les clés de session viennent de `rand()`/`random()`, générateurs non cryptographiques, et la
graine n'est jamais initialisée côté client. Une clé de session prévisible se devine ; une
fois devinée, elle vaut le mot de passe.

**Correction V15.** `crypto/rand` (256 bits), et la clé n'est **pas** stockée en base : seule
son empreinte SHA-256 l'est. Une fuite de la table des sessions ne permet plus d'usurper un
joueur.

---

## Moyens

### NIN-13 — Usurpation d'IP par `X-Forwarded-For`
**CWE-348** · `site-web/include/fonction.php`

L'adresse cliente est lue dans `X-Forwarded-For`, en-tête que n'importe quel client écrit
librement. Toute décision fondée sur l'IP — journal, blocage — se contourne en changeant
l'en-tête à chaque requête.
→ **V15** : `RemoteAddr` uniquement ; derrière un proxy, c'est au proxy de le réécrire.

### NIN-14 — Divulgation des erreurs SQL au client
**CWE-209** · plusieurs fichiers

`echo $link->error` renvoie au navigateur le message de MySQL : noms de tables, de colonnes,
souvent la requête. C'est exactement l'information dont un attaquant a besoin pour affiner
une injection sans tâtonner.
→ **V15** : détail journalisé côté serveur, message générique au client.

### NIN-15 — Aucune limitation de débit
**CWE-307** · tous les points d'entrée

Rien ne freine les tentatives de connexion. Avec la politique de mots de passe de NIN-07
(quatre caractères alphanumériques, soit ~1,7 million de combinaisons), un compte tombe en
quelques minutes.
→ **V15** : limitation par IP et par compte, stockée en base pour tenir avec plusieurs
instances.

### NIN-16 — Aucun jeton CSRF
**CWE-352** · tous les formulaires

Un site tiers peut faire exécuter au navigateur d'un joueur connecté n'importe quelle action
authentifiée.
→ **V15** : jeton en double soumission, `SameSite=Strict`.

### NIN-17 — Sessions sans expiration ni rotation
**CWE-613, CWE-384** · `nineteen_session`

La table ne comporte aucune date d'expiration : une clé de session est valable
indéfiniment. Elle n'est pas non plus renouvelée à la connexion, ce qui autorise la fixation
de session.
→ **V15** : durée de vie explicite, rotation à la connexion, révocation, purge périodique.

---

## Faibles

### NIN-18 — Secrets en dur dans le binaire distribué
**CWE-798** · `include/libWeb.c:231`
Les constantes du jeton sont lisibles par `strings`. Traité par NIN-02/NIN-06 : le client
V15 ne détient plus aucun secret durable.

### NIN-19 — Fichiers de test exposés en production
**CWE-489** · `site-web/test.html`, `tesst.html`, `css/style.css.tmp.html`
Retirés du dépôt.

### NIN-20 — Aucun en-tête de sécurité HTTP
**CWE-693**
Ni CSP, ni HSTS, ni `X-Content-Type-Options`, ni `X-Frame-Options`.
→ **V15** : tous posés (`server/internal/api/api.go`, `securityHeaders`).

### NIN-21 — Identifiants de base de données commités
**CWE-798** · `site-web/include/` (fichier de configuration)
Le dépôt du projet contient les identifiants MySQL de production. Même après retrait, ils
restent dans l'historique git.
→ **V15** : variables d'environnement, `.env` ignoré par git, `.env.example` fourni.
**Action à mener côté hébergement** : ces identifiants doivent être considérés comme
compromis et changés, indépendamment de tout ce qui précède.

---

## Trois hypothèses écartées après vérification

Un audit n'est utile que si l'on peut lui faire confiance sur ce qu'il **ne** signale pas.
Voici trois pistes qui semblaient sérieuses et qui n'en étaient pas.

**Le tampon du jeton `secure` déborde-t-il ?**
`char temp[MD5_SIZE*2]` fait 64 octets ; la chaîne formatée en fait 51, plus le zéro
terminal, soit 52. Elle tient. Le dépassement réel est ailleurs, d'un seul octet, et
concerne le tampon de destination — c'est NIN-09.

**`strcpy(*response, s.ptr)` (`libWeb.c:182`) déborde-t-il ?**
Non. L'allocation qui précède est
`malloc(sizeof(char) * strlen(s.ptr) + 1)` ; la précédence des opérateurs donne bien
`strlen + 1`, donc la place du zéro terminal est réservée. La ligne est correcte.

**Le format `%s` non borné de `construire_requete` déborde-t-il ?**
Les tampons de destination sont alloués à 512 octets et les entrées bornées à 24 caractères
en amont (`main.c:365-395`). La marge est suffisante dans le code tel qu'il est. C'est
fragile, pas exploitable.

---

## Priorités, si l'on devait remettre l'original en ligne demain

| Priorité | Action | Effort |
|---|---|---|
| P0 | Supprimer `disconnectAll.php` et la branche de débogage (NIN-04, 05) | minutes |
| P0 | Réactiver la vérification TLS côté client (NIN-03) | minutes |
| P0 | Changer les identifiants de base de données (NIN-21) | minutes |
| P0 | Borner la copie de la clé de session (NIN-22) | une ligne |
| P0 | Requêtes préparées partout (NIN-01) | 1 jour |
| P1 | Argon2id, politique de mots de passe, CSPRNG de sessions (NIN-07, 12) | heures |
| P1 | Échappement en sortie + CSP ; retirer `system()` (NIN-11, 08) | heures |
| P1 | Corriger `secure[32]` en `secure[33]` (NIN-09) | une ligne |
| P2 | Autorité serveur sur les scores (NIN-02, 06) | refonte |
| P2 | Limitation de débit, CSRF, expiration de sessions, en-têtes (NIN-13→20) | jours |

---

## En un paragraphe

Sept constats critiques, mais une seule cause de fond : **le serveur faisait confiance au
client**. Le jeton anti-rejeu, la protection mémoire du score et le hachage des mots de
passe défendaient tous une frontière placée du mauvais côté. La V15 déplace cette frontière
— le serveur ouvre la partie, en fixe les paramètres et recalcule le résultat — et le reste
des correctifs découle de ce choix. Le défaut le plus intéressant du lot n'est pourtant pas
architectural : c'est le dépassement d'un octet de NIN-09, qui se produisait à chaque
requête depuis cinq ans sans jamais avoir été remarqué, et qu'un simple passage sous
AddressSanitizer aurait signalé au premier lancement.
