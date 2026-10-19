/**
 * @file file_manager.h
 * @brief Persistance binaire du gestionnaire de réservations.
 *
 * Format du fichier (tous les entiers en little-endian, taille fixe,
 * indépendant de l'alignement et de l'endianness de la machine) :
 *
 *   EN-TÊTE (20 octets)
 *     [0..3]   magic       : "RSVM"
 *     [4..7]   version     : uint32 (FICHIER_VERSION)
 *     [8..11]  nb_records  : uint32
 *     [12..15] prochain_id : uint32
 *     [16..19] checksum    : uint32, FNV-1a 32 bits de toute la zone records
 *
 *   RECORDS (nb_records x TAILLE_RECORD octets)
 *     id (uint32) | jour, mois, annee, debut, fin (int32 x5)
 *     | salle[32] | utilisateur[64] | motif[128]
 *
 * La sauvegarde est atomique : écriture dans un fichier temporaire puis
 * rename(). Un crash pendant l'écriture ne corrompt jamais les données
 * précédentes.
 */
#ifndef FILE_MANAGER_H
#define FILE_MANAGER_H

#include "reservation.h"

#define FICHIER_DONNEES_DEFAUT "data/reservations.dat" /**< Chemin par défaut. */
#define FICHIER_MAGIC          "RSVM"                   /**< Signature (4 octets). */
#define FICHIER_VERSION        1u                       /**< Version du format.   */
#define FICHIER_MAX_RECORDS    100000u                  /**< Garde-fou au chargement. */

/**
 * @brief Sauvegarde toutes les réservations dans un fichier binaire.
 *
 * @param g      Gestionnaire à sauvegarder (non modifié).
 * @param chemin Chemin du fichier cible.
 * @return RES_OK, RES_ERR_PARAMETRE, RES_ERR_PLEIN ou RES_ERR_FICHIER.
 */
CodeRetour fichier_sauvegarder(const Gestionnaire *g, const char *chemin);

/**
 * @brief Charge les réservations depuis un fichier binaire.
 *
 * Le chargement est transactionnel : les données sont lues et validées
 * dans un gestionnaire temporaire. En cas de succès, l'ancien contenu de
 * `g` est libéré et remplacé ; en cas d'échec, `g` est laissé intact.
 *
 * @param g        Gestionnaire initialisé (gestionnaire_init) à remplir.
 * @param chemin   Chemin du fichier source.
 * @param nb_lues  [sortie, optionnel] nombre de réservations chargées.
 * @return RES_OK (y compris si le fichier n'existe pas encore : g est alors
 *         inchangé et *nb_lues vaut 0), RES_ERR_PARAMETRE, RES_ERR_FICHIER,
 *         RES_ERR_FORMAT ou RES_ERR_MEMOIRE.
 */
CodeRetour fichier_charger(Gestionnaire *g, const char *chemin, size_t *nb_lues);

#endif /* FILE_MANAGER_H */
