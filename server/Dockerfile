# Image du serveur Nineteen.
#
# Deux étages : compilation, puis une image « distroless » qui ne contient que
# le binaire. Pas de shell, pas de gestionnaire de paquets, donc quasiment
# aucune surface d'attaque à l'intérieur du conteneur.

# La version DOIT suivre celle que `go.mod` exige (`go 1.25.0`). L'image était
# restée en 1.24 : `docker compose up --build` échouait sur « go.mod requires
# go >= 1.25.0 », c'est-à-dire que la pile complète, celle que le README donne
# comme LA commande pour lancer le serveur, ne se construisait plus. Rien dans
# la CI ne bâtit cette image, donc rien ne le disait.
FROM golang:1.25-alpine AS build
WORKDIR /src

# Les dépendances d'abord : cette couche est réutilisée tant que go.mod et
# go.sum ne changent pas.
COPY go.mod go.sum ./
RUN go mod download

COPY . .
# CGO désactivé : le binaire est statique, donc exécutable dans une image vide.
# Les symboles de débogage sont retirés (-s -w) : 30 % de moins à transférer.
RUN CGO_ENABLED=0 GOOS=linux go build -trimpath -ldflags="-s -w" -o /nineteend ./cmd/nineteend \
	&& CGO_ENABLED=0 GOOS=linux go build -trimpath -ldflags="-s -w" -o /duelrelay ./cmd/duelrelay

FROM gcr.io/distroless/static-debian12:nonroot
COPY --from=build /nineteend /nineteend
COPY --from=build /duelrelay /duelrelay
# Utilisateur non privilégié fourni par l'image de base.
USER nonroot:nonroot
EXPOSE 8080
ENTRYPOINT ["/nineteend"]
