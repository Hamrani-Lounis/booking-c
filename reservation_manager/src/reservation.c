/**
 * @file reservation.c
 * @brief Logique métier : validation, détection de conflits, ajout,
 *        annulation et recherche des réservations.
 */
#include "reservation.h"

#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------ */
/* Fonctions internes                                                        */
/* ------------------------------------------------------------------------ */

/** Retourne 1 si l'année est bissextile, 0 sinon. */
static int est_bissextile(int annee)
{
    return (annee % 4 == 0 && annee % 100 != 0) || (annee % 400 == 0);
}

/** Nombre de jours du mois donné (mois supposé dans 1..12). */
static int jours_dans_mois(int mois, int annee)
{
    static const int jours[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (mois == 2 && est_bissextile(annee)) {
        return 29;
    }
    return jours[mois - 1];
}

/**
 * Vérifie qu'une chaîne est non vide, ne contient pas uniquement des espaces
 * et tient dans un tampon de `taille` octets (terminateur compris).
 */
static int chaine_valide(const char *s, size_t taille)
{
    size_t longueur;
    size_t i;
    int    contenu_visible = 0;

    if (s == NULL) {
        return 0;
    }
    longueur = strlen(s);
    if (longueur == 0 || longueur >= taille) {
        return 0;
    }
    for (i = 0; i < longueur; i++) {
        if (!isspace((unsigned char)s[i])) {
            contenu_visible = 1;
            break;
        }
    }
    return contenu_visible;
}

/** Copie sûre avec troncature et terminaison garantie. */
static void copier_chaine(char *dest, size_t taille, const char *src)
{
    size_t longueur = strlen(src);
    if (longueur >= taille) {
        longueur = taille - 1;
    }
    memcpy(dest, src, longueur);
    dest[longueur] = '\0';
}

/**
 * Garantit qu'il reste au moins une place libre dans le tableau.
 * Stratégie de croissance géométrique (x2) : coût amorti O(1) par ajout.
 * realloc est appliqué sur un pointeur temporaire pour ne jamais perdre
 * le bloc d'origine en cas d'échec.
 */
static CodeRetour assurer_capacite(Gestionnaire *g)
{
    size_t       nouvelle_capacite;
    Reservation *tmp;

    if (g->taille < g->capacite) {
        return RES_OK;
    }

    nouvelle_capacite = (g->capacite == 0) ? CAPACITE_INITIALE : g->capacite * 2;

    /* Protection contre le dépassement arithmétique de la taille allouée. */
    if (nouvelle_capacite < g->capacite ||
        nouvelle_capacite > SIZE_MAX / sizeof(Reservation)) {
        return RES_ERR_MEMOIRE;
    }

    tmp = realloc(g->reservations, nouvelle_capacite * sizeof(Reservation));
    if (tmp == NULL) {
        return RES_ERR_MEMOIRE; /* g->reservations reste valide. */
    }

    g->reservations = tmp;
    g->capacite     = nouvelle_capacite;
    return RES_OK;
}

/** Comparateur qsort : date, début, salle, puis id. */
static int comparer_reservations(const void *pa, const void *pb)
{
    const Reservation *a = (const Reservation *)pa;
    const Reservation *b = (const Reservation *)pb;
    int cmp;

    cmp = date_comparer(&a->creneau.date, &b->creneau.date);
    if (cmp != 0) {
        return cmp;
    }
    if (a->creneau.debut != b->creneau.debut) {
        return (a->creneau.debut < b->creneau.debut) ? -1 : 1;
    }
    cmp = salle_comparer(a->salle, b->salle);
    if (cmp != 0) {
        return cmp;
    }
    if (a->id != b->id) {
        return (a->id < b->id) ? -1 : 1;
    }
    return 0;
}

/* ------------------------------------------------------------------------ */
/* Cycle de vie                                                              */
/* ------------------------------------------------------------------------ */

CodeRetour gestionnaire_init(Gestionnaire *g)
{
    if (g == NULL) {
        return RES_ERR_PARAMETRE;
    }

    g->reservations = malloc(CAPACITE_INITIALE * sizeof(Reservation));
    if (g->reservations == NULL) {
        g->taille      = 0;
        g->capacite    = 0;
        g->prochain_id = 1;
        return RES_ERR_MEMOIRE;
    }

    g->taille      = 0;
    g->capacite    = CAPACITE_INITIALE;
    g->prochain_id = 1;
    return RES_OK;
}

void gestionnaire_liberer(Gestionnaire *g)
{
    if (g == NULL) {
        return;
    }
    free(g->reservations); /* free(NULL) est sans effet : appel idempotent. */
    g->reservations = NULL;
    g->taille       = 0;
    g->capacite     = 0;
    g->prochain_id  = 1;
}

void gestionnaire_trier(Gestionnaire *g)
{
    if (g == NULL || g->reservations == NULL || g->taille < 2) {
        return;
    }
    qsort(g->reservations, g->taille, sizeof(Reservation), comparer_reservations);
}

/* ------------------------------------------------------------------------ */
/* Dates et créneaux                                                         */
/* ------------------------------------------------------------------------ */

int date_valide(const Date *d)
{
    if (d == NULL) {
        return 0;
    }
    if (d->annee < ANNEE_MIN || d->annee > ANNEE_MAX) {
        return 0;
    }
    if (d->mois < 1 || d->mois > 12) {
        return 0;
    }
    if (d->jour < 1 || d->jour > jours_dans_mois(d->mois, d->annee)) {
        return 0;
    }
    return 1;
}

int date_comparer(const Date *a, const Date *b)
{
    if (a->annee != b->annee) {
        return (a->annee < b->annee) ? -1 : 1;
    }
    if (a->mois != b->mois) {
        return (a->mois < b->mois) ? -1 : 1;
    }
    if (a->jour != b->jour) {
        return (a->jour < b->jour) ? -1 : 1;
    }
    return 0;
}

int creneau_valide(const Creneau *c)
{
    if (c == NULL || !date_valide(&c->date)) {
        return 0;
    }
    if (c->debut < HEURE_OUVERTURE_MIN || c->fin > HEURE_FERMETURE_MIN) {
        return 0;
    }
    if (c->fin - c->debut < DUREE_MIN_RESERVATION) {
        return 0;
    }
    return 1;
}

int creneaux_chevauchent(const Creneau *a, const Creneau *b)
{
    if (date_comparer(&a->date, &b->date) != 0) {
        return 0;
    }
    /* Intervalles semi-ouverts [debut, fin[ : chevauchement strict. */
    return (a->debut < b->fin) && (b->debut < a->fin);
}

int salle_comparer(const char *a, const char *b)
{
    unsigned char ca;
    unsigned char cb;

    for (;;) {
        ca = (unsigned char)tolower((unsigned char)*a);
        cb = (unsigned char)tolower((unsigned char)*b);
        if (ca != cb || ca == '\0') {
            return (int)ca - (int)cb;
        }
        a++;
        b++;
    }
}

/* ------------------------------------------------------------------------ */
/* Logique métier                                                            */
/* ------------------------------------------------------------------------ */

CodeRetour reservation_ajouter(Gestionnaire *g,
                               const char *salle,
                               const char *utilisateur,
                               const char *motif,
                               const Creneau *creneau,
                               unsigned int *id_cree,
                               const Reservation **conflit)
{
    size_t       i;
    CodeRetour   code;
    Reservation *nouvelle;

    if (conflit != NULL) {
        *conflit = NULL;
    }
    if (g == NULL || creneau == NULL ||
        !chaine_valide(salle, TAILLE_SALLE) ||
        !chaine_valide(utilisateur, TAILLE_UTILISATEUR) ||
        !chaine_valide(motif, TAILLE_MOTIF)) {
        return RES_ERR_PARAMETRE;
    }
    if (!date_valide(&creneau->date)) {
        return RES_ERR_DATE;
    }
    if (!creneau_valide(creneau)) {
        return RES_ERR_CRENEAU;
    }

    /* Détection des conflits : même salle + créneaux qui se chevauchent. */
    for (i = 0; i < g->taille; i++) {
        const Reservation *r = &g->reservations[i];
        if (salle_comparer(r->salle, salle) == 0 &&
            creneaux_chevauchent(&r->creneau, creneau)) {
            if (conflit != NULL) {
                *conflit = r;
            }
            return RES_ERR_CONFLIT;
        }
    }

    if (g->prochain_id == UINT_MAX) {
        return RES_ERR_PLEIN;
    }

    code = assurer_capacite(g);
    if (code != RES_OK) {
        return code;
    }

    nouvelle = &g->reservations[g->taille];
    memset(nouvelle, 0, sizeof(*nouvelle)); /* octets de bourrage à zéro. */
    nouvelle->id      = g->prochain_id;
    nouvelle->creneau = *creneau;
    copier_chaine(nouvelle->salle, sizeof(nouvelle->salle), salle);
    copier_chaine(nouvelle->utilisateur, sizeof(nouvelle->utilisateur), utilisateur);
    copier_chaine(nouvelle->motif, sizeof(nouvelle->motif), motif);

    g->taille++;
    g->prochain_id++;

    if (id_cree != NULL) {
        *id_cree = nouvelle->id;
    }

    gestionnaire_trier(g); /* invalide le pointeur `nouvelle`, plus utilisé. */
    return RES_OK;
}

CodeRetour reservation_annuler(Gestionnaire *g, unsigned int id)
{
    size_t i;

    if (g == NULL || id == 0) {
        return RES_ERR_PARAMETRE;
    }

    for (i = 0; i < g->taille; i++) {
        if (g->reservations[i].id == id) {
            /* Décalage des éléments suivants : l'ordre trié est conservé.
             * memmove gère correctement le recouvrement des zones. */
            if (i + 1 < g->taille) {
                memmove(&g->reservations[i],
                        &g->reservations[i + 1],
                        (g->taille - i - 1) * sizeof(Reservation));
            }
            g->taille--;
            return RES_OK;
        }
    }
    return RES_ERR_INTROUVABLE;
}

const Reservation *reservation_trouver(const Gestionnaire *g, unsigned int id)
{
    size_t i;

    if (g == NULL) {
        return NULL;
    }
    for (i = 0; i < g->taille; i++) {
        if (g->reservations[i].id == id) {
            return &g->reservations[i];
        }
    }
    return NULL;
}

CodeRetour reservation_rechercher(const Gestionnaire *g,
                                  const Date *filtre_date,
                                  const char *filtre_salle,
                                  const Reservation ***resultats,
                                  size_t *nb_resultats)
{
    const Reservation **tab;
    size_t              n = 0;
    size_t              i;

    if (g == NULL || resultats == NULL || nb_resultats == NULL) {
        return RES_ERR_PARAMETRE;
    }
    *resultats    = NULL;
    *nb_resultats = 0;

    if (filtre_date != NULL && !date_valide(filtre_date)) {
        return RES_ERR_DATE;
    }
    if (g->taille == 0) {
        return RES_OK;
    }

    /* Allocation au pire cas (toutes les réservations correspondent). */
    if (g->taille > SIZE_MAX / sizeof(*tab)) {
        return RES_ERR_MEMOIRE;
    }
    tab = malloc(g->taille * sizeof(*tab));
    if (tab == NULL) {
        return RES_ERR_MEMOIRE;
    }

    for (i = 0; i < g->taille; i++) {
        const Reservation *r = &g->reservations[i];
        if (filtre_date != NULL && date_comparer(&r->creneau.date, filtre_date) != 0) {
            continue;
        }
        if (filtre_salle != NULL && salle_comparer(r->salle, filtre_salle) != 0) {
            continue;
        }
        tab[n++] = r;
    }

    if (n == 0) {
        free(tab);
        return RES_OK;
    }

    /* Ajustement à la taille exacte ; en cas d'échec le bloc surdimensionné
     * reste parfaitement utilisable, on le conserve donc tel quel. */
    if (n < g->taille) {
        const Reservation **ajuste = realloc(tab, n * sizeof(*tab));
        if (ajuste != NULL) {
            tab = ajuste;
        }
    }

    *resultats    = tab;
    *nb_resultats = n;
    return RES_OK;
}

const char *code_retour_message(CodeRetour code)
{
    switch (code) {
    case RES_OK:              return "Succes";
    case RES_ERR_MEMOIRE:     return "Echec d'allocation memoire";
    case RES_ERR_PARAMETRE:   return "Parametre invalide (champ vide ou trop long)";
    case RES_ERR_DATE:        return "Date invalide ou hors bornes";
    case RES_ERR_CRENEAU:     return "Creneau invalide (horaires 08:00-20:00, 15 min minimum)";
    case RES_ERR_CONFLIT:     return "Conflit avec une reservation existante";
    case RES_ERR_INTROUVABLE: return "Reservation introuvable";
    case RES_ERR_PLEIN:       return "Limite de capacite atteinte";
    case RES_ERR_FICHIER:     return "Erreur d'acces au fichier";
    case RES_ERR_FORMAT:      return "Fichier de donnees corrompu ou incompatible";
    default:                  return "Erreur inconnue";
    }
}
