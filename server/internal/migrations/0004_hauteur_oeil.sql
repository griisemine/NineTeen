-- 0004 — la hauteur d'œil, pour que les autres joueurs aient des PIEDS.
--
-- CE QUE CETTE COLONNE RÉPARE
-- ---------------------------
-- `presence.y` est, et a toujours été, la position de la CAMÉRA du joueur — pas
-- celle du sol sous lui. Tant qu'un pair n'était qu'une étiquette flottante, la
-- distinction n'avait aucune conséquence : on posait le texte au-dessus de `y`
-- et personne ne comptait les centimètres.
--
-- Depuis qu'un pair a un CORPS, il faut savoir où poser ses pieds, et `y` seul
-- ne le dit pas. Le client soustrayait alors une hauteur debout écrite en dur,
-- ce qui marche jusqu'à ce que quelqu'un s'accroupisse : sa caméra descend de
-- 39 cm, et son personnage s'enfonce d'autant dans la moquette pour tous les
-- autres.
--
-- `eye` est la hauteur de l'œil AU-DESSUS DES PIEDS, en mètres. Les pieds sont
-- alors exactement `y - eye`, accroupissement compris, sans que personne ait à
-- deviner.
--
-- POURQUOI UNE COLONNE ET NON UN CHANGEMENT DE SENS DE `y`
-- --------------------------------------------------------
-- Publier directement les pieds dans `y` aurait évité cette migration. Mais
-- `y` est déjà émis par tous les clients déployés, et en changer le sens ferait
-- flotter en l'air, pour tous les autres, le joueur d'une version antérieure —
-- un mètre soixante au-dessus du sol, sans que rien ne l'explique. Une colonne
-- de plus, avec un défaut à zéro qui signifie « ce client ne la publie pas »,
-- laisse les deux générations coexister : le client retombe alors sur sa propre
-- hauteur debout, ce qui est faux de 39 cm au pire et juste le reste du temps.
--
-- Le défaut est ZÉRO et non une taille plausible, exprès : zéro n'est pas une
-- hauteur d'œil possible, donc il se distingue d'une valeur publiée. Une valeur
-- de repli écrite ici aurait été indiscernable d'une vraie, et le client
-- n'aurait plus jamais su à qui il avait affaire.

BEGIN;

ALTER TABLE presence
    ADD COLUMN IF NOT EXISTS eye real NOT NULL DEFAULT 0;

COMMIT;
