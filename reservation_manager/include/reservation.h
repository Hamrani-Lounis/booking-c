/**
 * @file reservation.h
 * @brief Structures de données et API métier du gestionnaire de réservations.
 *
 * Ce module ne fait AUCUNE entrée/sortie console ni fichier : il ne manipule
 * que la mémoire. Les modules file_manager (persistance) et main (interface)
 * s'appuient sur lui.
 *
 * Conventions :
 *  - Les heures sont stockées en minutes depuis minuit (ex. 14h30 -> 870).
 *  - Un créneau est un intervalle semi-ouvert [debut, fin[ : une réservation
 *    10:00-12:00 et une autre 12:00-14:00 dans la même salle ne sont PAS en
 *    conflit.
 *  - Le tableau de réservations est maintenu trié (date, heure de début,
 *    salle) après chaque ajout et chaque chargement.
 */
#ifndef RESERVATION_H
#define RESERVATION_H

#include <stddef.h>

/* ------------------------------------------------------------------------ */
/* Constantes                                                                */
/* ------------------------------------------------------------------------ */

#define TAILLE_SALLE          32   /**< Taille du tampon nom de salle ('\0' inclus). */
#define TAILLE_UTILISATEUR    64   /**< Taille du tampon utilisateur ('\0' inclus).  */
#define TAILLE_MOTIF         128   /**< Taille du tampon motif ('\0' inclus).        */

#define HEURE_OUVERTURE_MIN  (8 * 60)   /**< Ouverture des salles : 08:00. */
#define HEURE_FERMETURE_MIN  (20 * 60)  /**< Fermeture des salles : 20:00. */
#define DUREE_MIN_RESERVATION 15        /**< Durée minimale en minutes.    */

#define ANNEE_MIN 2000  /**< Année minimale acceptée. */
#define ANNEE_MAX 2099  /**< Année maximale acceptée. */

#define CAPACITE_INITIALE 8  /**< Capacité allouée à l'initialisation. */

/* ------------------------------------------------------------------------ */
/* Structures de données                                                     */
/* ------------------------------------------------------------------------ */

/** @brief Date calendaire. */
typedef struct {
    int jour;   /**< 1..31 selon le mois.  */
    int mois;   /**< 1..12.                */
    int annee;  /**< ANNEE_MIN..ANNEE_MAX. */
} Date;

/** @brief Créneau horaire [debut, fin[ sur une date donnée. */
typedef struct {
    Date date;  /**< Jour de la réservation.               */
    int  debut; /**< Début en minutes depuis minuit.        */
    int  fin;   /**< Fin en minutes depuis minuit (exclue). */
} Creneau;

/** @brief Réservation d'une ressource partagée (salle ou poste). */
typedef struct {
    unsigned int id;                          /**< Identifiant unique (> 0). */
    char         salle[TAILLE_SALLE];         /**< Ex. "B204", "LAB-POSTE-12". */
    char         utilisateur[TAILLE_UTILISATEUR];
    char         motif[TAILLE_MOTIF];
    Creneau      creneau;
} Reservation;

/**
 * @brief Conteneur dynamique des réservations.
 *
 * Invariants : taille <= capacite ; reservations pointe vers un bloc de
 * `capacite` éléments alloué sur le tas (ou NULL après libération) ;
 * prochain_id est strictement supérieur à tout id présent.
 */
typedef struct {
    Reservation  *reservations; /**< Tableau dynamique (malloc/realloc). */
    size_t        taille;       /**< Nombre d'éléments utilisés.         */
    size_t        capacite;     /**< Nombre d'éléments alloués.          */
    unsigned int  prochain_id;  /**< Prochain identifiant à attribuer.   */
} Gestionnaire;

/** @brief Codes de retour communs à tous les modules. */
typedef enum {
    RES_OK = 0,           /**< Succès.                                    */
    RES_ERR_MEMOIRE,      /**< Échec d'allocation dynamique.              */
    RES_ERR_PARAMETRE,    /**< Pointeur NULL ou chaîne vide / trop longue. */
    RES_ERR_DATE,         /**< Date inexistante ou hors bornes.           */
    RES_ERR_CRENEAU,      /**< Horaires invalides ou hors ouverture.      */
    RES_ERR_CONFLIT,      /**< Chevauchement avec une réservation.        */
    RES_ERR_INTROUVABLE,  /**< Identifiant inconnu.                       */
    RES_ERR_PLEIN,        /**< Limite atteinte (identifiants, fichier).   */
    RES_ERR_FICHIER,      /**< Erreur d'ouverture / lecture / écriture.   */
    RES_ERR_FORMAT        /**< Fichier binaire corrompu ou incompatible.  */
} CodeRetour;

/* ------------------------------------------------------------------------ */
/* Cycle de vie du gestionnaire                                              */
/* ------------------------------------------------------------------------ */

/**
 * @brief Initialise un gestionnaire vide (alloue CAPACITE_INITIALE éléments).
 * @param g Gestionnaire non initialisé.
 * @return RES_OK, RES_ERR_PARAMETRE ou RES_ERR_MEMOIRE.
 */
CodeRetour gestionnaire_init(Gestionnaire *g);

/**
 * @brief Libère toute la mémoire du gestionnaire et le remet à zéro.
 *        Appel sans danger sur un gestionnaire déjà libéré ou NULL.
 * @param g Gestionnaire à libérer.
 */
void gestionnaire_liberer(Gestionnaire *g);

/**
 * @brief Trie les réservations par date, heure de début, salle puis id.
 * @param g Gestionnaire.
 */
void gestionnaire_trier(Gestionnaire *g);

/* ------------------------------------------------------------------------ */
/* Outils sur dates et créneaux                                              */
/* ------------------------------------------------------------------------ */

/**
 * @brief Vérifie qu'une date existe (années bissextiles comprises).
 * @return 1 si valide, 0 sinon.
 */
int date_valide(const Date *d);

/**
 * @brief Compare deux dates.
 * @return <0 si a < b, 0 si égales, >0 si a > b.
 */
int date_comparer(const Date *a, const Date *b);

/**
 * @brief Vérifie un créneau : date valide, horaires dans la plage
 *        d'ouverture, durée >= DUREE_MIN_RESERVATION.
 * @return 1 si valide, 0 sinon.
 */
int creneau_valide(const Creneau *c);

/**
 * @brief Indique si deux créneaux se chevauchent (même date et
 *        intersection non vide des intervalles [debut, fin[).
 * @return 1 en cas de chevauchement, 0 sinon.
 */
int creneaux_chevauchent(const Creneau *a, const Creneau *b);

/**
 * @brief Compare deux noms de salle sans tenir compte de la casse.
 * @return <0, 0 ou >0 à la manière de strcmp.
 */
int salle_comparer(const char *a, const char *b);

/* ------------------------------------------------------------------------ */
/* Logique métier                                                            */
/* ------------------------------------------------------------------------ */

/**
 * @brief Ajoute une réservation après validation et détection de conflit.
 *
 * @param g           Gestionnaire.
 * @param salle       Nom de la salle (1 à TAILLE_SALLE-1 caractères).
 * @param utilisateur Nom du demandeur (1 à TAILLE_UTILISATEUR-1 caractères).
 * @param motif       Motif (1 à TAILLE_MOTIF-1 caractères).
 * @param creneau     Créneau demandé.
 * @param id_cree     [sortie, optionnel] identifiant attribué.
 * @param conflit     [sortie, optionnel] réservation en conflit si
 *                    RES_ERR_CONFLIT, NULL sinon. Le pointeur reste valide
 *                    jusqu'à la prochaine modification du gestionnaire.
 * @return RES_OK, RES_ERR_PARAMETRE, RES_ERR_DATE, RES_ERR_CRENEAU,
 *         RES_ERR_CONFLIT, RES_ERR_PLEIN ou RES_ERR_MEMOIRE.
 */
CodeRetour reservation_ajouter(Gestionnaire *g,
                               const char *salle,
                               const char *utilisateur,
                               const char *motif,
                               const Creneau *creneau,
                               unsigned int *id_cree,
                               const Reservation **conflit);

/**
 * @brief Annule (supprime) la réservation d'identifiant donné.
 *        L'ordre des autres réservations est conservé.
 * @return RES_OK, RES_ERR_PARAMETRE ou RES_ERR_INTROUVABLE.
 */
CodeRetour reservation_annuler(Gestionnaire *g, unsigned int id);

/**
 * @brief Recherche une réservation par identifiant.
 * @return Pointeur vers la réservation (valide jusqu'à la prochaine
 *         modification), ou NULL si absente.
 */
const Reservation *reservation_trouver(const Gestionnaire *g, unsigned int id);

/**
 * @brief Recherche les réservations correspondant aux filtres.
 *
 * @param g            Gestionnaire.
 * @param filtre_date  Date exacte à filtrer, ou NULL pour toutes les dates.
 * @param filtre_salle Salle à filtrer (insensible à la casse), ou NULL.
 * @param resultats    [sortie] tableau alloué de pointeurs (NULL si aucun
 *                     résultat). L'APPELANT DOIT LE LIBÉRER avec free().
 *                     Les pointeurs restent valides jusqu'à la prochaine
 *                     modification du gestionnaire.
 * @param nb_resultats [sortie] nombre de résultats.
 * @return RES_OK, RES_ERR_PARAMETRE, RES_ERR_DATE ou RES_ERR_MEMOIRE.
 */
CodeRetour reservation_rechercher(const Gestionnaire *g,
                                  const Date *filtre_date,
                                  const char *filtre_salle,
                                  const Reservation ***resultats,
                                  size_t *nb_resultats);

/**
 * @brief Message lisible associé à un code de retour.
 * @return Chaîne statique (ne pas libérer).
 */
const char *code_retour_message(CodeRetour code);

#endif /* RESERVATION_H */
