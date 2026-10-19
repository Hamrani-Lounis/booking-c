/**
 * @file file_manager.c
 * @brief Sauvegarde et chargement binaires, sérialisation explicite et
 *        portable (aucun fwrite direct de struct : pas de dépendance au
 *        bourrage mémoire ni à l'endianness).
 */
#include "file_manager.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------ */
/* Constantes du format                                                      */
/* ------------------------------------------------------------------------ */

#define TAILLE_ENTETE 20u
#define TAILLE_RECORD (4u + 5u * 4u + TAILLE_SALLE + TAILLE_UTILISATEUR + TAILLE_MOTIF)

#define OFF_MAGIC     0u
#define OFF_VERSION   4u
#define OFF_NB        8u
#define OFF_PROCHAIN  12u
#define OFF_CHECKSUM  16u

#define SUFFIXE_TMP   ".tmp"

/* ------------------------------------------------------------------------ */
/* Encodage little-endian                                                    */
/* ------------------------------------------------------------------------ */

static void ecrire_u32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v & 0xFFu);
    p[1] = (unsigned char)((v >> 8) & 0xFFu);
    p[2] = (unsigned char)((v >> 16) & 0xFFu);
    p[3] = (unsigned char)((v >> 24) & 0xFFu);
}

static uint32_t lire_u32(const unsigned char *p)
{
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

/* Les int32 sont stockés en complément à deux via leur image uint32. */
static void ecrire_i32(unsigned char *p, int32_t v)
{
    ecrire_u32(p, (uint32_t)v);
}

static int32_t lire_i32(const unsigned char *p)
{
    uint32_t u = lire_u32(p);
    if (u <= (uint32_t)INT32_MAX) {
        return (int32_t)u;
    }
    /* Conversion définie (pas de comportement dépendant de l'implémentation). */
    return (int32_t)(-(int64_t)(UINT32_MAX - u) - 1);
}

/** Somme de contrôle FNV-1a 32 bits. */
static uint32_t fnv1a(const unsigned char *data, size_t n)
{
    uint32_t h = 2166136261u;
    size_t   i;
    for (i = 0; i < n; i++) {
        h ^= data[i];
        h *= 16777619u;
    }
    return h;
}

/* ------------------------------------------------------------------------ */
/* Sérialisation d'un enregistrement                                         */
/* ------------------------------------------------------------------------ */

static void serialiser(const Reservation *r, unsigned char *p)
{
    size_t off = 0;

    memset(p, 0, TAILLE_RECORD);
    ecrire_u32(p + off, (uint32_t)r->id);                 off += 4;
    ecrire_i32(p + off, (int32_t)r->creneau.date.jour);   off += 4;
    ecrire_i32(p + off, (int32_t)r->creneau.date.mois);   off += 4;
    ecrire_i32(p + off, (int32_t)r->creneau.date.annee);  off += 4;
    ecrire_i32(p + off, (int32_t)r->creneau.debut);       off += 4;
    ecrire_i32(p + off, (int32_t)r->creneau.fin);         off += 4;
    memcpy(p + off, r->salle, TAILLE_SALLE);              off += TAILLE_SALLE;
    memcpy(p + off, r->utilisateur, TAILLE_UTILISATEUR);  off += TAILLE_UTILISATEUR;
    memcpy(p + off, r->motif, TAILLE_MOTIF);
}

/** Vérifie qu'un tampon de chaîne contient un '\0' et un contenu non vide. */
static int tampon_chaine_valide(const char *s, size_t taille)
{
    return memchr(s, '\0', taille) != NULL && s[0] != '\0';
}

/**
 * Désérialise et valide un enregistrement.
 * @return 1 si l'enregistrement est cohérent, 0 sinon.
 */
static int deserialiser(const unsigned char *p, Reservation *r)
{
    size_t off = 0;

    memset(r, 0, sizeof(*r));
    r->id                 = (unsigned int)lire_u32(p + off); off += 4;
    r->creneau.date.jour  = (int)lire_i32(p + off);          off += 4;
    r->creneau.date.mois  = (int)lire_i32(p + off);          off += 4;
    r->creneau.date.annee = (int)lire_i32(p + off);          off += 4;
    r->creneau.debut      = (int)lire_i32(p + off);          off += 4;
    r->creneau.fin        = (int)lire_i32(p + off);          off += 4;
    memcpy(r->salle, p + off, TAILLE_SALLE);                 off += TAILLE_SALLE;
    memcpy(r->utilisateur, p + off, TAILLE_UTILISATEUR);     off += TAILLE_UTILISATEUR;
    memcpy(r->motif, p + off, TAILLE_MOTIF);

    return r->id != 0
        && creneau_valide(&r->creneau)
        && tampon_chaine_valide(r->salle, TAILLE_SALLE)
        && tampon_chaine_valide(r->utilisateur, TAILLE_UTILISATEUR)
        && tampon_chaine_valide(r->motif, TAILLE_MOTIF);
}

/* ------------------------------------------------------------------------ */
/* Sauvegarde                                                                */
/* ------------------------------------------------------------------------ */

CodeRetour fichier_sauvegarder(const Gestionnaire *g, const char *chemin)
{
    unsigned char  entete[TAILLE_ENTETE];
    unsigned char *zone = NULL;
    size_t         taille_zone;
    size_t         i;
    char          *chemin_tmp;
    size_t         longueur;
    FILE          *f;
    int            ok = 1;

    if (g == NULL || chemin == NULL || chemin[0] == '\0') {
        return RES_ERR_PARAMETRE;
    }
    if (g->taille > FICHIER_MAX_RECORDS) {
        return RES_ERR_PLEIN;
    }

    /* 1. Sérialisation complète en mémoire. */
    taille_zone = g->taille * TAILLE_RECORD;
    if (taille_zone > 0) {
        zone = malloc(taille_zone);
        if (zone == NULL) {
            return RES_ERR_MEMOIRE;
        }
        for (i = 0; i < g->taille; i++) {
            serialiser(&g->reservations[i], zone + i * TAILLE_RECORD);
        }
    }

    memcpy(entete + OFF_MAGIC, FICHIER_MAGIC, 4);
    ecrire_u32(entete + OFF_VERSION, FICHIER_VERSION);
    ecrire_u32(entete + OFF_NB, (uint32_t)g->taille);
    ecrire_u32(entete + OFF_PROCHAIN, (uint32_t)g->prochain_id);
    ecrire_u32(entete + OFF_CHECKSUM, fnv1a(zone, taille_zone));

    /* 2. Construction du chemin temporaire "<chemin>.tmp". */
    longueur   = strlen(chemin);
    chemin_tmp = malloc(longueur + sizeof(SUFFIXE_TMP));
    if (chemin_tmp == NULL) {
        free(zone);
        return RES_ERR_MEMOIRE;
    }
    memcpy(chemin_tmp, chemin, longueur);
    memcpy(chemin_tmp + longueur, SUFFIXE_TMP, sizeof(SUFFIXE_TMP));

    /* 3. Écriture dans le fichier temporaire, chaque retour vérifié. */
    f = fopen(chemin_tmp, "wb");
    if (f == NULL) {
        free(zone);
        free(chemin_tmp);
        return RES_ERR_FICHIER;
    }

    if (fwrite(entete, 1, TAILLE_ENTETE, f) != TAILLE_ENTETE) {
        ok = 0;
    }
    if (ok && taille_zone > 0 && fwrite(zone, 1, taille_zone, f) != taille_zone) {
        ok = 0;
    }
    if (ok && fflush(f) != 0) {
        ok = 0;
    }
    if (fclose(f) != 0) { /* fclose peut révéler une erreur d'écriture différée. */
        ok = 0;
    }

    free(zone);

    /* 4. Remplacement atomique, ou nettoyage du temporaire en cas d'échec. */
    if (ok && rename(chemin_tmp, chemin) != 0) {
        /* Certains systèmes refusent rename() vers un fichier existant. */
        if (remove(chemin) != 0 || rename(chemin_tmp, chemin) != 0) {
            ok = 0;
        }
    }
    if (!ok) {
        remove(chemin_tmp);
    }

    free(chemin_tmp);
    return ok ? RES_OK : RES_ERR_FICHIER;
}

/* ------------------------------------------------------------------------ */
/* Chargement                                                                */
/* ------------------------------------------------------------------------ */

CodeRetour fichier_charger(Gestionnaire *g, const char *chemin, size_t *nb_lues)
{
    FILE          *f;
    unsigned char  entete[TAILLE_ENTETE];
    unsigned char *zone = NULL;
    unsigned char  octet_en_trop;
    uint32_t       nb;
    uint32_t       prochain;
    size_t         taille_zone;
    size_t         i;
    size_t         j;
    Gestionnaire   tmp;
    CodeRetour     code = RES_OK;

    if (nb_lues != NULL) {
        *nb_lues = 0;
    }
    if (g == NULL || chemin == NULL || chemin[0] == '\0') {
        return RES_ERR_PARAMETRE;
    }

    errno = 0;
    f = fopen(chemin, "rb");
    if (f == NULL) {
#ifdef ENOENT
        if (errno == ENOENT) {
            return RES_OK; /* Premier lancement : aucun fichier, base vide. */
        }
        return RES_ERR_FICHIER;
#else
        return RES_OK;
#endif
    }

    /* 1. En-tête. */
    if (fread(entete, 1, TAILLE_ENTETE, f) != TAILLE_ENTETE) {
        fclose(f);
        return RES_ERR_FORMAT;
    }
    if (memcmp(entete + OFF_MAGIC, FICHIER_MAGIC, 4) != 0 ||
        lire_u32(entete + OFF_VERSION) != FICHIER_VERSION) {
        fclose(f);
        return RES_ERR_FORMAT;
    }

    nb       = lire_u32(entete + OFF_NB);
    prochain = lire_u32(entete + OFF_PROCHAIN);
    if (nb > FICHIER_MAX_RECORDS || prochain == 0 || prochain <= nb) {
        fclose(f);
        return RES_ERR_FORMAT;
    }

    /* 2. Zone des enregistrements + contrôle d'intégrité. */
    taille_zone = (size_t)nb * TAILLE_RECORD;
    if (taille_zone > 0) {
        zone = malloc(taille_zone);
        if (zone == NULL) {
            fclose(f);
            return RES_ERR_MEMOIRE;
        }
        if (fread(zone, 1, taille_zone, f) != taille_zone) {
            code = ferror(f) ? RES_ERR_FICHIER : RES_ERR_FORMAT;
        }
    }
    /* Le fichier doit se terminer exactement après le dernier record. */
    if (code == RES_OK && fread(&octet_en_trop, 1, 1, f) != 0) {
        code = RES_ERR_FORMAT;
    }
    if (code == RES_OK && ferror(f)) {
        code = RES_ERR_FICHIER;
    }
    fclose(f); /* Lecture seule : un échec de fclose n'affecte pas les données lues. */

    if (code == RES_OK && fnv1a(zone, taille_zone) != lire_u32(entete + OFF_CHECKSUM)) {
        code = RES_ERR_FORMAT;
    }
    if (code != RES_OK) {
        free(zone);
        return code;
    }

    /* 3. Désérialisation dans un gestionnaire temporaire (transaction). */
    tmp.reservations = NULL;
    tmp.taille       = 0;
    tmp.capacite     = 0;
    tmp.prochain_id  = (unsigned int)prochain;

    if (nb > 0) {
        tmp.reservations = malloc((size_t)nb * sizeof(Reservation));
        if (tmp.reservations == NULL) {
            free(zone);
            return RES_ERR_MEMOIRE;
        }
        tmp.capacite = nb;
    }

    for (i = 0; i < nb && code == RES_OK; i++) {
        Reservation *r = &tmp.reservations[i];

        if (!deserialiser(zone + i * TAILLE_RECORD, r) || r->id >= tmp.prochain_id) {
            code = RES_ERR_FORMAT;
            break;
        }
        /* Cohérence globale : ids uniques et aucun conflit enregistré. */
        for (j = 0; j < i; j++) {
            const Reservation *autre = &tmp.reservations[j];
            if (autre->id == r->id ||
                (salle_comparer(autre->salle, r->salle) == 0 &&
                 creneaux_chevauchent(&autre->creneau, &r->creneau))) {
                code = RES_ERR_FORMAT;
                break;
            }
        }
        tmp.taille = i + 1;
    }

    free(zone);

    if (code != RES_OK) {
        free(tmp.reservations);
        return code;
    }

    /* 4. Commit : remplacement du contenu de g. */
    gestionnaire_trier(&tmp);
    gestionnaire_liberer(g);
    *g = tmp;

    if (g->reservations == NULL) {
        /* Fichier valide mais vide : on garde une capacité initiale. */
        Reservation *bloc = malloc(CAPACITE_INITIALE * sizeof(Reservation));
        if (bloc != NULL) {
            g->reservations = bloc;
            g->capacite     = CAPACITE_INITIALE;
        }
        /* En cas d'échec, assurer_capacite() réallouera au prochain ajout. */
    }

    if (nb_lues != NULL) {
        *nb_lues = g->taille;
    }
    return RES_OK;
}
