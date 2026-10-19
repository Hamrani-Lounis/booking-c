# Gestionnaire de réservations de ressources partagées

SAE — Architecture et programmation système · BUT 2 Informatique
Langage : **C99** · Compilateur : **GCC** · Interface : **console**

Application de réservation de postes de labo et de salles informatiques, avec
détection des conflits de créneau et persistance dans un fichier binaire.

---

## 1. Arborescence

```
reservation_manager/
├── Makefile
├── README.md
├── include/
│   ├── reservation.h      # Structures (Date, Creneau, Reservation, Gestionnaire) + API métier
│   └── file_manager.h     # API de persistance + description du format binaire
├── src/
│   ├── reservation.c      # Logique métier : validation, conflits, ajout, annulation, recherche
│   ├── file_manager.c     # Sérialisation, sauvegarde atomique, chargement transactionnel
│   └── main.c             # Boucle principale, menu, saisies sécurisées
├── obj/                   # (généré) fichiers objets .o et dépendances .d
├── bin/                   # (généré) exécutable reservation_manager
└── data/                  # (généré) reservations.dat
```

## 2. Compilation et exécution

```bash
make                              # compile (flags : -Wall -Wextra -std=c99 -pedantic -Werror)
./bin/reservation_manager         # utilise data/reservations.dat
./bin/reservation_manager autre.dat   # fichier de données alternatif
```

| Règle            | Effet                                                               |
|------------------|---------------------------------------------------------------------|
| `make` / `make all` | Compilation séparée de chaque module, création de `obj/`, `bin/`, `data/` |
| `make run`       | Compile puis lance                                                  |
| `make debug`     | Recompile avec `-g -O0 -fsanitize=address,undefined`                |
| `make valgrind`  | Lance l'exécutable sous Valgrind (`--leak-check=full`)              |
| `make clean`     | Supprime `obj/` et `bin/` (les données sont conservées)             |
| `make distclean` | Supprime aussi `data/`                                              |

Les dépendances d'en-têtes sont générées automatiquement (`-MMD -MP`) :
modifier `reservation.h` recompile uniquement les modules qui l'incluent.

## 3. Choix d'architecture

### 3.1 Séparation des responsabilités

| Module          | Rôle                                   | E/S console | E/S fichier |
|-----------------|----------------------------------------|:-----------:|:-----------:|
| `reservation.c` | Règles métier, mémoire                 | non         | non         |
| `file_manager.c`| Format binaire                         | non         | oui         |
| `main.c`        | Interaction utilisateur                | oui         | via API     |

Le cœur métier ne fait **aucun `printf`** : il renvoie des codes `CodeRetour`
et est donc testable indépendamment de l'interface. Les dépendances vont
dans un seul sens : `main → file_manager → reservation`.

### 3.2 Modèle de données

- Heures stockées en **minutes depuis minuit** (`14:30` → `870`) : les
  comparaisons de créneaux deviennent de simples comparaisons d'entiers.
- Créneau = intervalle **semi-ouvert `[début, fin[`** : `10:00-12:00` et
  `12:00-14:00` dans la même salle **ne sont pas** en conflit.
- Conflit ⇔ même salle (insensible à la casse : `b204` = `B204`), même date,
  et `a.debut < b.fin && b.debut < a.fin`.
- Règles : ouverture 08:00–20:00, durée minimale 15 min, années 2000–2099,
  dates réelles vérifiées (31/02 refusé, 29/02 accepté les années bissextiles).
- Identifiants uniques et **jamais réutilisés** (`prochain_id` persisté).

### 3.3 Gestion de la mémoire

- `Gestionnaire` contient un **tableau dynamique** (`malloc` puis `realloc`).
- Croissance **géométrique ×2** : coût amorti O(1) par ajout.
- `realloc` toujours appliqué à un **pointeur temporaire** : en cas d'échec,
  le bloc d'origine n'est jamais perdu.
- Contrôle de **dépassement arithmétique** avant chaque allocation
  (`n > SIZE_MAX / sizeof(T)`).
- `reservation_rechercher` renvoie un tableau de pointeurs alloué dont la
  **propriété est transférée à l'appelant**, qui le libère avec `free`
  (contrat écrit dans le `.h`).
- `gestionnaire_liberer` est **idempotent** (remise à `NULL` après `free`).
- Tableau maintenu **trié** (date, heure, salle) : planning affiché dans
  l'ordre chronologique et calcul direct des plages libres.

Vérifié : `valgrind --leak-check=full` → `0 bytes in 0 blocks` sur toutes
les sessions de test (ajouts, conflits, annulations, 500 réservations,
fichier corrompu, fin d'entrée par Ctrl+D) ; build `make debug` sans aucune
alerte ASan/UBSan.

### 3.4 Format binaire

Pas de `fwrite` direct des `struct` : leur représentation dépend du
bourrage (padding) et de l'endianness, donc du compilateur et de la machine.
Chaque champ est **sérialisé explicitement en little-endian**, à taille fixe.

```
EN-TÊTE (20 octets)
  0  magic        "RSVM"
  4  version      uint32   (1)
  8  nb_records   uint32
 12  prochain_id  uint32
 16  checksum     uint32   FNV-1a de la zone records

RECORD (248 octets) × nb_records
  id (u32) | jour | mois | annee | debut | fin (i32 ×5)
  | salle[32] | utilisateur[64] | motif[128]
```

Taille du fichier = `20 + 248 × n` octets.

**Robustesse :**

- **Sauvegarde atomique** : écriture dans `reservations.dat.tmp`, vérification
  de chaque `fwrite`, `fflush` et `fclose`, puis `rename()`. Une coupure en
  pleine écriture laisse l'ancien fichier intact.
- **Chargement transactionnel** : lecture et validation dans un gestionnaire
  temporaire ; le gestionnaire réel n'est remplacé qu'en cas de succès total.
- **Validation complète** : signature, version, somme de contrôle, taille
  exacte du fichier, chaînes terminées par `'\0'`, créneaux valides, ids
  uniques, absence de conflits enregistrés.
- Fichier absent → premier lancement, base vide. Fichier corrompu → arrêt
  **sans écraser** le fichier.
- Sauvegarde automatique à la sortie (option 0 ou Ctrl+D) uniquement si des
  modifications sont en attente.

### 3.5 Saisies sécurisées

Toutes les saisies passent par `fgets` puis `sscanf`/`strtoul` sur la ligne
lue (jamais de `scanf` direct sur `stdin`) : lignes trop longues purgées,
caractères parasites rejetés (`10:00abc`), ligne vide = abandon de
l'opération, fin de flux gérée proprement.

## 4. Exemple de session

```
$ make
mkdir -p obj
gcc -Iinclude -Wall -Wextra -std=c99 -pedantic -Werror -O2 -MMD -MP -c src/file_manager.c -o obj/file_manager.o
gcc -Iinclude -Wall -Wextra -std=c99 -pedantic -Werror -O2 -MMD -MP -c src/main.c -o obj/main.o
gcc -Iinclude -Wall -Wextra -std=c99 -pedantic -Werror -O2 -MMD -MP -c src/reservation.c -o obj/reservation.o
mkdir -p bin
gcc obj/file_manager.o obj/main.o obj/reservation.o -o bin/reservation_manager
mkdir -p data

$ ./bin/reservation_manager
Fichier 'data/reservations.dat' : 0 reservation(s) chargee(s).

============ GESTIONNAIRE DE RESERVATIONS ============
  0 reservation(s) en memoire
------------------------------------------------------
  1. Afficher le planning complet
  2. Planning d'une date
  3. Planning d'une salle
  4. Ajouter une reservation
  5. Annuler une reservation
  6. Sauvegarder
  0. Quitter (sauvegarde automatique)
======================================================
Votre choix : 4
  -- Nouvelle reservation (ligne vide = abandon) --
  Salle / poste      : B204
  Date (JJ/MM/AAAA)  : 20/10/2026
  Debut (HH:MM)      : 10:00
  Fin   (HH:MM)      : 12:00
  Utilisateur        : M. Laroche
  Motif              : TP Reseaux Cisco
  [OK] Reservation #1 enregistree.

============ GESTIONNAIRE DE RESERVATIONS ============
  1 reservation(s) en memoire  [modifications non sauvegardees]
  ...
Votre choix : 4
  -- Nouvelle reservation (ligne vide = abandon) --
  Salle / poste      : b204
  Date (JJ/MM/AAAA)  : 20/10/2026
  Debut (HH:MM)      : 11:00
  Fin   (HH:MM)      : 13:00
  Utilisateur        : Seif
  Motif              : Projet SAE
  [CONFLIT] La salle b204 est deja reservee sur ce creneau :
    #1  20/10/2026  10:00-12:00  salle B204  (M. Laroche : TP Reseaux Cisco)
  Plages libres pour b204 le 20/10/2026 : [08:00-10:00] [12:00-20:00]

Votre choix : 4
  -- Nouvelle reservation (ligne vide = abandon) --
  Salle / poste      : B204
  Date (JJ/MM/AAAA)  : 20/10/2026
  Debut (HH:MM)      : 12:00
  Fin   (HH:MM)      : 14:00
  Utilisateur        : Seif
  Motif              : Projet SAE
  [OK] Reservation #2 enregistree.

Votre choix : 4
  -- Nouvelle reservation (ligne vide = abandon) --
  Salle / poste      : LAB-12
  Date (JJ/MM/AAAA)  : 31/02/2026
  ! Date invalide (2000-2099).
  Date (JJ/MM/AAAA)  : 21/10/2026
  Debut (HH:MM)      : 19:00
  Fin   (HH:MM)      : 21:00
  Utilisateur        : Seif
  Motif              : Revision
  [REFUS] Creneau invalide (horaires 08:00-20:00, 15 min minimum).

Votre choix : 1
+------+------------+-------------+------------+----------------------+--------------------------------+
| ID   | Date       | Horaire     | Salle      | Utilisateur          | Motif                          |
+------+------------+-------------+------------+----------------------+--------------------------------+
|    1 | 20/10/2026 | 10:00-12:00 | B204       | M. Laroche           | TP Reseaux Cisco               |
|    2 | 20/10/2026 | 12:00-14:00 | B204       | Seif                 | Projet SAE                     |
+------+------------+-------------+------------+----------------------+--------------------------------+
  2 reservation(s).

Votre choix : 0
Sauvegarde automatique : 2 reservation(s) ecrite(s) dans 'data/reservations.dat'.
Au revoir.

$ ls -l data/reservations.dat        # 20 + 2 x 248 = 516 octets
-rw-r--r-- 1 user user 516 Oct 20 10:05 data/reservations.dat

$ ./bin/reservation_manager
Fichier 'data/reservations.dat' : 2 reservation(s) chargee(s).
...
Votre choix : 5
  ID de la reservation a annuler : 1
    #1  20/10/2026  10:00-12:00  salle B204  (M. Laroche : TP Reseaux Cisco)
  Confirmer l'annulation ? (o/n) : o
  [OK] Reservation #1 annulee.
```

Points illustrés : détection du conflit malgré la casse différente (`b204`),
affichage de la réservation bloquante et des plages libres, créneau
adjacent `12:00-14:00` accepté (intervalle semi-ouvert), date impossible et
horaire hors ouverture refusés, persistance entre deux lancements.

## 5. Pistes d'évolution

- Recherche par utilisateur et par plage de dates.
- Index par salle (table de hachage) pour une détection de conflits en
  O(k) au lieu de O(n) sur de gros volumes.
- Verrou de fichier (`flock`, POSIX) pour un usage multi-processus.
- Tests unitaires automatisés du module `reservation.c` (il est déjà isolé
  de toute E/S, donc directement testable).
