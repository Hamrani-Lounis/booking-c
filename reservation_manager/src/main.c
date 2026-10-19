/**
 * @file main.c
 * @brief Interface console : boucle principale, menu interactif, saisies
 *        sécurisées. Chargement automatique au démarrage, sauvegarde
 *        automatique à l'arrêt.
 *
 * Usage : ./bin/reservation_manager [fichier_donnees]
 */
#include "file_manager.h"
#include "reservation.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAILLE_LIGNE 256

/* Résultat d'une saisie utilisateur. */
typedef enum {
    SAISIE_OK = 0,
    SAISIE_VIDE,     /* ligne vide : interprétée comme "annuler" */
    SAISIE_INVALIDE,
    SAISIE_EOF       /* fin de l'entrée standard (Ctrl+D) */
} EtatSaisie;

/* ------------------------------------------------------------------------ */
/* Saisies sécurisées (fgets uniquement : jamais de scanf direct sur stdin)  */
/* ------------------------------------------------------------------------ */

/**
 * Lit une ligne complète dans `buf`, retire le '\n' et les espaces de bord.
 * Une ligne trop longue est intégralement consommée puis rejetée.
 */
static EtatSaisie lire_ligne(const char *invite, char *buf, size_t taille)
{
    size_t longueur;
    size_t debut = 0;
    int    c;

    if (invite != NULL) {
        printf("%s", invite);
        fflush(stdout);
    }
    if (fgets(buf, (int)taille, stdin) == NULL) {
        return SAISIE_EOF;
    }

    longueur = strlen(buf);
    if (longueur > 0 && buf[longueur - 1] == '\n') {
        buf[--longueur] = '\0';
    } else if (!feof(stdin)) {
        /* Ligne trop longue : on vide le reste pour ne pas polluer la suite. */
        while ((c = getchar()) != '\n' && c != EOF) {
        }
        buf[0] = '\0';
        return SAISIE_INVALIDE;
    }

    /* Suppression des espaces / '\r' en fin et en début de ligne. */
    while (longueur > 0 && (buf[longueur - 1] == ' ' || buf[longueur - 1] == '\t' ||
                            buf[longueur - 1] == '\r')) {
        buf[--longueur] = '\0';
    }
    while (buf[debut] == ' ' || buf[debut] == '\t') {
        debut++;
    }
    if (debut > 0) {
        memmove(buf, buf + debut, longueur - debut + 1);
        longueur -= debut;
    }

    return (longueur == 0) ? SAISIE_VIDE : SAISIE_OK;
}

/** Saisie d'une date au format JJ/MM/AAAA, validée calendrier compris. */
static EtatSaisie lire_date(const char *invite, Date *d)
{
    char       buf[TAILLE_LIGNE];
    char       reste;
    EtatSaisie etat = lire_ligne(invite, buf, sizeof(buf));

    if (etat != SAISIE_OK) {
        return etat;
    }
    /* " %c" après la date détecte tout caractère parasite en fin de saisie. */
    if (sscanf(buf, "%d/%d/%d %c", &d->jour, &d->mois, &d->annee, &reste) != 3 ||
        !date_valide(d)) {
        return SAISIE_INVALIDE;
    }
    return SAISIE_OK;
}

/** Saisie d'une heure HH:MM convertie en minutes depuis minuit. */
static EtatSaisie lire_heure(const char *invite, int *minutes)
{
    char       buf[TAILLE_LIGNE];
    char       reste;
    int        h;
    int        m;
    EtatSaisie etat = lire_ligne(invite, buf, sizeof(buf));

    if (etat != SAISIE_OK) {
        return etat;
    }
    if (sscanf(buf, "%d:%d %c", &h, &m, &reste) != 2 ||
        h < 0 || h > 24 || m < 0 || m > 59 || (h == 24 && m != 0)) {
        return SAISIE_INVALIDE;
    }
    *minutes = h * 60 + m;
    return SAISIE_OK;
}

/** Saisie d'un entier non signé strictement positif. */
static EtatSaisie lire_id(const char *invite, unsigned int *id)
{
    char          buf[TAILLE_LIGNE];
    char         *fin;
    unsigned long v;
    EtatSaisie    etat = lire_ligne(invite, buf, sizeof(buf));

    if (etat != SAISIE_OK) {
        return etat;
    }
    if (buf[0] == '-') {
        return SAISIE_INVALIDE;
    }
    v = strtoul(buf, &fin, 10);
    if (*fin != '\0' || v == 0 || v > (unsigned long)(unsigned int)-1) {
        return SAISIE_INVALIDE;
    }
    *id = (unsigned int)v;
    return SAISIE_OK;
}

/**
 * Saisie d'un champ texte borné. Redemande tant que la saisie est trop
 * longue. Retourne SAISIE_VIDE si l'utilisateur abandonne (ligne vide).
 */
static EtatSaisie lire_texte(const char *invite, char *dest, size_t taille)
{
    char       buf[TAILLE_LIGNE];
    EtatSaisie etat;

    for (;;) {
        etat = lire_ligne(invite, buf, sizeof(buf));
        if (etat == SAISIE_OK && strlen(buf) >= taille) {
            printf("  ! Trop long (%lu caracteres maximum).\n", (unsigned long)(taille - 1));
            continue;
        }
        if (etat == SAISIE_INVALIDE) {
            printf("  ! Saisie trop longue, recommencez.\n");
            continue;
        }
        if (etat == SAISIE_OK) {
            strcpy(dest, buf); /* longueur vérifiée juste au-dessus. */
        }
        return etat;
    }
}

/* ------------------------------------------------------------------------ */
/* Affichage                                                                 */
/* ------------------------------------------------------------------------ */

static void afficher_ligne_separation(void)
{
    printf("+------+------------+-------------+------------+----------------------+--------------------------------+\n");
}

static void afficher_entete_tableau(void)
{
    afficher_ligne_separation();
    printf("| %-4s | %-10s | %-11s | %-10s | %-20s | %-30s |\n",
           "ID", "Date", "Horaire", "Salle", "Utilisateur", "Motif");
    afficher_ligne_separation();
}

static void afficher_reservation_ligne(const Reservation *r)
{
    printf("| %4u | %02d/%02d/%04d | %02d:%02d-%02d:%02d | %-10.10s | %-20.20s | %-30.30s |\n",
           r->id,
           r->creneau.date.jour, r->creneau.date.mois, r->creneau.date.annee,
           r->creneau.debut / 60, r->creneau.debut % 60,
           r->creneau.fin / 60, r->creneau.fin % 60,
           r->salle, r->utilisateur, r->motif);
}

static void afficher_reservation_detail(const Reservation *r)
{
    printf("    #%u  %02d/%02d/%04d  %02d:%02d-%02d:%02d  salle %s  (%s : %s)\n",
           r->id,
           r->creneau.date.jour, r->creneau.date.mois, r->creneau.date.annee,
           r->creneau.debut / 60, r->creneau.debut % 60,
           r->creneau.fin / 60, r->creneau.fin % 60,
           r->salle, r->utilisateur, r->motif);
}

/** Affiche un tableau de résultats de recherche. */
static void afficher_liste(const Reservation **tab, size_t n)
{
    size_t i;

    if (n == 0) {
        printf("  (aucune reservation)\n");
        return;
    }
    afficher_entete_tableau();
    for (i = 0; i < n; i++) {
        afficher_reservation_ligne(tab[i]);
    }
    afficher_ligne_separation();
    printf("  %lu reservation(s).\n", (unsigned long)n);
}

/** Recherche + affichage, avec libération systématique du résultat. */
static void afficher_planning(const Gestionnaire *g, const Date *date, const char *salle)
{
    const Reservation **res = NULL;
    size_t              n   = 0;
    CodeRetour          code;

    code = reservation_rechercher(g, date, salle, &res, &n);
    if (code != RES_OK) {
        printf("  ! Erreur : %s\n", code_retour_message(code));
        return;
    }
    afficher_liste(res, n);
    free(res); /* le tableau de pointeurs appartient à l'appelant. */
}

/**
 * Propose les plages libres d'une salle pour une date donnée, calculées
 * à partir des réservations triées par heure de début.
 */
static void afficher_creneaux_libres(const Gestionnaire *g, const Date *date, const char *salle)
{
    const Reservation **res      = NULL;
    size_t              n        = 0;
    size_t              i;
    int                 curseur  = HEURE_OUVERTURE_MIN;
    int                 trouve   = 0;

    if (reservation_rechercher(g, date, salle, &res, &n) != RES_OK) {
        return;
    }

    printf("  Plages libres pour %s le %02d/%02d/%04d :", salle, date->jour, date->mois, date->annee);
    for (i = 0; i <= n; i++) {
        int borne = (i < n) ? res[i]->creneau.debut : HEURE_FERMETURE_MIN;
        if (borne - curseur >= DUREE_MIN_RESERVATION) {
            printf(" [%02d:%02d-%02d:%02d]", curseur / 60, curseur % 60, borne / 60, borne % 60);
            trouve = 1;
        }
        if (i < n && res[i]->creneau.fin > curseur) {
            curseur = res[i]->creneau.fin;
        }
    }
    printf("%s\n", trouve ? "" : " aucune");
    free(res);
}

/* ------------------------------------------------------------------------ */
/* Actions du menu                                                           */
/* ------------------------------------------------------------------------ */

/* Chaque action retourne 0 normalement, -1 si stdin est fermé (EOF). */

static int action_planning_jour(const Gestionnaire *g)
{
    Date       d;
    EtatSaisie e = lire_date("  Date (JJ/MM/AAAA) : ", &d);

    if (e == SAISIE_EOF) return -1;
    if (e != SAISIE_OK) {
        printf("  ! Date invalide.\n");
        return 0;
    }
    afficher_planning(g, &d, NULL);
    return 0;
}

static int action_planning_salle(const Gestionnaire *g)
{
    char       salle[TAILLE_SALLE];
    EtatSaisie e = lire_texte("  Salle : ", salle, sizeof(salle));

    if (e == SAISIE_EOF) return -1;
    if (e != SAISIE_OK) {
        printf("  Operation annulee.\n");
        return 0;
    }
    afficher_planning(g, NULL, salle);
    return 0;
}

static int action_ajouter(Gestionnaire *g, int *modifie)
{
    char               salle[TAILLE_SALLE];
    char               utilisateur[TAILLE_UTILISATEUR];
    char               motif[TAILLE_MOTIF];
    Creneau            c;
    EtatSaisie         e;
    CodeRetour         code;
    unsigned int       id      = 0;
    const Reservation *conflit = NULL;

    printf("  -- Nouvelle reservation (ligne vide = abandon) --\n");

    e = lire_texte("  Salle / poste      : ", salle, sizeof(salle));
    if (e == SAISIE_EOF) return -1;
    if (e != SAISIE_OK) { printf("  Operation annulee.\n"); return 0; }

    do {
        e = lire_date("  Date (JJ/MM/AAAA)  : ", &c.date);
        if (e == SAISIE_INVALIDE) printf("  ! Date invalide (%d-%d).\n", ANNEE_MIN, ANNEE_MAX);
    } while (e == SAISIE_INVALIDE);
    if (e == SAISIE_EOF) return -1;
    if (e != SAISIE_OK) { printf("  Operation annulee.\n"); return 0; }

    do {
        e = lire_heure("  Debut (HH:MM)      : ", &c.debut);
        if (e == SAISIE_INVALIDE) printf("  ! Heure invalide.\n");
    } while (e == SAISIE_INVALIDE);
    if (e == SAISIE_EOF) return -1;
    if (e != SAISIE_OK) { printf("  Operation annulee.\n"); return 0; }

    do {
        e = lire_heure("  Fin   (HH:MM)      : ", &c.fin);
        if (e == SAISIE_INVALIDE) printf("  ! Heure invalide.\n");
    } while (e == SAISIE_INVALIDE);
    if (e == SAISIE_EOF) return -1;
    if (e != SAISIE_OK) { printf("  Operation annulee.\n"); return 0; }

    e = lire_texte("  Utilisateur        : ", utilisateur, sizeof(utilisateur));
    if (e == SAISIE_EOF) return -1;
    if (e != SAISIE_OK) { printf("  Operation annulee.\n"); return 0; }

    e = lire_texte("  Motif              : ", motif, sizeof(motif));
    if (e == SAISIE_EOF) return -1;
    if (e != SAISIE_OK) { printf("  Operation annulee.\n"); return 0; }

    code = reservation_ajouter(g, salle, utilisateur, motif, &c, &id, &conflit);
    switch (code) {
    case RES_OK:
        *modifie = 1;
        printf("  [OK] Reservation #%u enregistree.\n", id);
        break;
    case RES_ERR_CONFLIT:
        printf("  [CONFLIT] La salle %s est deja reservee sur ce creneau :\n", salle);
        if (conflit != NULL) {
            afficher_reservation_detail(conflit);
        }
        afficher_creneaux_libres(g, &c.date, salle);
        break;
    default:
        printf("  [REFUS] %s.\n", code_retour_message(code));
        break;
    }
    return 0;
}

static int action_annuler(Gestionnaire *g, int *modifie)
{
    unsigned int       id;
    char               rep[TAILLE_LIGNE];
    const Reservation *r;
    EtatSaisie         e;
    CodeRetour         code;

    e = lire_id("  ID de la reservation a annuler : ", &id);
    if (e == SAISIE_EOF) return -1;
    if (e == SAISIE_VIDE) { printf("  Operation annulee.\n"); return 0; }
    if (e != SAISIE_OK)   { printf("  ! Identifiant invalide.\n"); return 0; }

    r = reservation_trouver(g, id);
    if (r == NULL) {
        printf("  ! %s (#%u).\n", code_retour_message(RES_ERR_INTROUVABLE), id);
        return 0;
    }
    afficher_reservation_detail(r);

    e = lire_ligne("  Confirmer l'annulation ? (o/n) : ", rep, sizeof(rep));
    if (e == SAISIE_EOF) return -1;
    if (e != SAISIE_OK || (rep[0] != 'o' && rep[0] != 'O') || rep[1] != '\0') {
        printf("  Annulation abandonnee.\n");
        return 0;
    }

    code = reservation_annuler(g, id);
    if (code == RES_OK) {
        *modifie = 1;
        printf("  [OK] Reservation #%u annulee.\n", id);
    } else {
        printf("  ! %s.\n", code_retour_message(code));
    }
    return 0;
}

static void action_sauvegarder(const Gestionnaire *g, const char *chemin, int *modifie)
{
    CodeRetour code = fichier_sauvegarder(g, chemin);

    if (code == RES_OK) {
        *modifie = 0;
        printf("  [OK] %lu reservation(s) sauvegardee(s) dans '%s'.\n",
               (unsigned long)g->taille, chemin);
    } else {
        printf("  ! Sauvegarde impossible dans '%s' : %s.\n", chemin, code_retour_message(code));
        printf("    (le dossier parent existe-t-il ? 'make' cree le dossier data/)\n");
    }
}

static void afficher_menu(const Gestionnaire *g, int modifie)
{
    printf("\n============ GESTIONNAIRE DE RESERVATIONS ============\n");
    printf("  %lu reservation(s) en memoire%s\n",
           (unsigned long)g->taille, modifie ? "  [modifications non sauvegardees]" : "");
    printf("------------------------------------------------------\n");
    printf("  1. Afficher le planning complet\n");
    printf("  2. Planning d'une date\n");
    printf("  3. Planning d'une salle\n");
    printf("  4. Ajouter une reservation\n");
    printf("  5. Annuler une reservation\n");
    printf("  6. Sauvegarder\n");
    printf("  0. Quitter (sauvegarde automatique)\n");
    printf("======================================================\n");
}

/* ------------------------------------------------------------------------ */
/* Point d'entrée                                                            */
/* ------------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    Gestionnaire g;
    const char  *chemin  = (argc > 1) ? argv[1] : FICHIER_DONNEES_DEFAUT;
    char         choix[TAILLE_LIGNE];
    int          modifie = 0;
    int          continuer = 1;
    int          code_sortie = EXIT_SUCCESS;
    size_t       nb_lues = 0;
    CodeRetour   code;
    EtatSaisie   e;

    if (gestionnaire_init(&g) != RES_OK) {
        fprintf(stderr, "Erreur fatale : %s.\n", code_retour_message(RES_ERR_MEMOIRE));
        return EXIT_FAILURE;
    }

    /* Chargement automatique au démarrage. */
    code = fichier_charger(&g, chemin, &nb_lues);
    if (code != RES_OK) {
        fprintf(stderr, "Erreur au chargement de '%s' : %s.\n", chemin, code_retour_message(code));
        fprintf(stderr, "Le fichier n'a pas ete modifie. Abandon pour proteger les donnees.\n");
        gestionnaire_liberer(&g);
        return EXIT_FAILURE;
    }
    printf("Fichier '%s' : %lu reservation(s) chargee(s).\n", chemin, (unsigned long)nb_lues);

    while (continuer) {
        afficher_menu(&g, modifie);
        e = lire_ligne("Votre choix : ", choix, sizeof(choix));
        if (e == SAISIE_EOF) {
            printf("\nFin de l'entree standard.\n");
            break;
        }
        if (e != SAISIE_OK || choix[1] != '\0') {
            printf("  ! Choix invalide.\n");
            continue;
        }

        switch (choix[0]) {
        case '1':
            afficher_planning(&g, NULL, NULL);
            break;
        case '2':
            if (action_planning_jour(&g) < 0) continuer = 0;
            break;
        case '3':
            if (action_planning_salle(&g) < 0) continuer = 0;
            break;
        case '4':
            if (action_ajouter(&g, &modifie) < 0) continuer = 0;
            break;
        case '5':
            if (action_annuler(&g, &modifie) < 0) continuer = 0;
            break;
        case '6':
            action_sauvegarder(&g, chemin, &modifie);
            break;
        case '0':
            continuer = 0;
            break;
        default:
            printf("  ! Choix invalide.\n");
            break;
        }
    }

    /* Sauvegarde automatique à l'arrêt, uniquement si nécessaire. */
    if (modifie) {
        code = fichier_sauvegarder(&g, chemin);
        if (code == RES_OK) {
            printf("Sauvegarde automatique : %lu reservation(s) ecrite(s) dans '%s'.\n",
                   (unsigned long)g.taille, chemin);
        } else {
            fprintf(stderr, "ECHEC de la sauvegarde automatique : %s.\n", code_retour_message(code));
            code_sortie = EXIT_FAILURE;
        }
    }

    gestionnaire_liberer(&g);
    printf("Au revoir.\n");
    return code_sortie;
}
