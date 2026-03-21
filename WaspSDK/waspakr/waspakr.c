/*
 * WasPakr v1.0.0 - Quake PAK Archiver
 * waspakr.c - Main implementation
 *
 * Commands:
 *   pack    <output.pak> <dir|file...>   Create a new PAK from files/dirs
 *   extract <input.pak>  [dest_dir]      Extract all files from a PAK
 *   list    <input.pak>                  List contents of a PAK
 *   replace <input.pak>  <name> <file>   Replace a file inside a PAK
 *   add     <input.pak>  <file> [name]   Add a file to an existing PAK
 *   remove  <input.pak>  <name>          Remove a file from a PAK
 *   check   <input.pak>                  Validate PAK integrity
 *
 */

#define _POSIX_C_SOURCE 200809L
#include "waspakr.h"

/* -------------------------------------------------------------------------
 * PAK archive read / write
 * -------------------------------------------------------------------------*/

/* Read a PAK file into a PakArchive. Loads directory only (not file data). */
static PakArchive *pak_open(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) die("cannot open '%s': %s", path, strerror(errno));

    pak_header_t hdr;
    if (fread(&hdr, sizeof(hdr), 1, f) != 1)
        die("'%s': failed to read header", path);
    if (memcmp(hdr.magic, PAK_MAGIC, 4) != 0)
        die("'%s': not a valid PAK file (bad magic)", path);

    uint32_t num = hdr.dir_size / PAK_ENTRY_SIZE;
    if (num == 0) { fclose(f); PakArchive *a = calloc(1, sizeof(PakArchive)); return a; }
    if (num > PAK_MAX_ENTRIES)
        die("'%s': too many entries (%u)", path, num);

    fseek(f, hdr.dir_offset, SEEK_SET);
    pak_entry_t *raw = malloc(num * sizeof(pak_entry_t));
    if (!raw) die("out of memory");
    if (fread(raw, sizeof(pak_entry_t), num, f) != num)
        die("'%s': truncated directory", path);
    fclose(f);

    PakArchive *a = calloc(1, sizeof(PakArchive));
    a->entries = malloc(num * sizeof(PakEntry));
    a->count   = (int)num;
    a->cap     = (int)num;

    for (uint32_t i = 0; i < num; i++) {
        strncpy(a->entries[i].name, raw[i].name, PAK_NAME_LEN - 1);
        a->entries[i].name[PAK_NAME_LEN-1] = '\0';
        normalize_slashes(a->entries[i].name);
        a->entries[i].offset   = raw[i].offset;
        a->entries[i].size     = raw[i].size;
        a->entries[i].src_path = NULL;
        a->entries[i].data     = NULL;
    }
    free(raw);
    return a;
}

/* Load file data for a specific entry from an open PAK */
static uint8_t *pak_read_entry(const char *pak_path, const PakEntry *e) {
    FILE *f = fopen(pak_path, "rb");
    if (!f) die("cannot open '%s': %s", pak_path, strerror(errno));
    fseek(f, e->offset, SEEK_SET);
    uint8_t *buf = malloc(e->size);
    if (!buf) die("out of memory");
    if (fread(buf, 1, e->size, f) != e->size)
        die("'%s': failed to read entry '%s'", pak_path, e->name);
    fclose(f);
    return buf;
}

static void pak_free(PakArchive *a) {
    if (!a) return;
    for (int i = 0; i < a->count; i++) {
        free(a->entries[i].src_path);
        free(a->entries[i].data);
    }
    free(a->entries);
    free(a);
}

static void archive_push(PakArchive *a, const char *pak_name,
                          const char *src_path, uint32_t size) {
    if (a->count >= a->cap) {
        a->cap = a->cap ? a->cap * 2 : 64;
        a->entries = realloc(a->entries, a->cap * sizeof(PakEntry));
        if (!a->entries) die("out of memory");
    }
    PakEntry *e = &a->entries[a->count++];
    memset(e, 0, sizeof(*e));
    strncpy(e->name, pak_name, PAK_NAME_LEN - 1);
    normalize_slashes(e->name);
    to_lower(e->name);
    e->src_path = src_path ? strdup(src_path) : NULL;
    e->size     = size;
    e->data     = NULL;
}

/* Write a PakArchive to disk. Sources are read from src_path or data. */
static bool pak_write(PakArchive *a, const char *output_path) {
    FILE *f = fopen(output_path, "wb");
    if (!f) die("cannot create '%s': %s", output_path, strerror(errno));

    /* Reserve space for header */
    pak_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    fwrite(&hdr, sizeof(hdr), 1, f);

    /* Write all file data, record offsets */
    for (int i = 0; i < a->count; i++) {
        PakEntry *e = &a->entries[i];
        e->offset = (uint32_t)ftell(f);

        if (e->data) {
            fwrite(e->data, 1, e->size, f);
        } else if (e->src_path) {
            long sz;
            uint8_t *buf = read_file(e->src_path, &sz);
            if (!buf) die("cannot read '%s': %s", e->src_path, strerror(errno));
            fwrite(buf, 1, sz, f);
            free(buf);
        }
    }

    /* Write directory */
    uint32_t dir_offset = (uint32_t)ftell(f);
    for (int i = 0; i < a->count; i++) {
        pak_entry_t raw;
        memset(&raw, 0, sizeof(raw));
        memset(raw.name, 0, PAK_NAME_LEN);
        memcpy(raw.name, a->entries[i].name, PAK_NAME_LEN - 1);
        raw.offset = a->entries[i].offset;
        raw.size   = a->entries[i].size;
        fwrite(&raw, sizeof(raw), 1, f);
    }
    uint32_t dir_size = (uint32_t)(a->count * PAK_ENTRY_SIZE);

    /* Patch header */
    fseek(f, 0, SEEK_SET);
    memcpy(hdr.magic, PAK_MAGIC, 4);
    hdr.dir_offset = dir_offset;
    hdr.dir_size   = dir_size;
    fwrite(&hdr, sizeof(hdr), 1, f);

    fclose(f);
    return true;
}

/* -------------------------------------------------------------------------
 * Directory walker — recursively adds files from a directory
 * -------------------------------------------------------------------------*/
static void walk_dir(PakArchive *a, const char *disk_path,
                     const char *pak_prefix) {
    DIR *d = opendir(disk_path);
    if (!d) { warn("cannot open directory '%s': %s", disk_path, strerror(errno)); return; }

    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;

        char full[1024], pak_name_buf[512];
        snprintf(full,         sizeof(full),         "%s/%s", disk_path, de->d_name);
        snprintf(pak_name_buf, sizeof(pak_name_buf), "%s%s%s",
                 pak_prefix,
                 pak_prefix[0] ? "/" : "",
                 de->d_name);
        char pak_name[PAK_NAME_LEN];
        memset(pak_name, 0, PAK_NAME_LEN);
        memcpy(pak_name, pak_name_buf, PAK_NAME_LEN - 1);

        if (is_dir(full)) {
            walk_dir(a, full, pak_name);
        } else {
            long sz = file_size(full);
            if (sz < 0) { warn("cannot stat '%s', skipping", full); continue; }
            archive_push(a, pak_name, full, (uint32_t)sz);
        }
    }
    closedir(d);
}

/* -------------------------------------------------------------------------
 * cmd_pack — create a new PAK
 * Usage: waspakr pack <output.pak> <source...>
 *
 * Sources can be:
 *   - a directory  (recursively added, preserving relative paths)
 *   - a file       (added at root level of PAK)
 *   - file@pakname (added with explicit PAK path)
 * -------------------------------------------------------------------------*/
static int cmd_pack(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "usage: waspakr pack <output.pak> <dir|file[@pakname]...>\n");
        return 1;
    }

    const char *output = argv[0];
    PakArchive  a = {0};

    int total_files = 0;
    long total_bytes = 0;

    for (int i = 1; i < argc; i++) {
        /* Support file@pakname syntax */
        char src[1024], pak_name[PAK_NAME_LEN];
        char *at = strchr(argv[i], '@');
        if (at) {
            size_t srclen = (size_t)(at - argv[i]);
            if (srclen >= sizeof(src)) die("path too long: '%s'", argv[i]);
            strncpy(src, argv[i], srclen); src[srclen] = '\0';
            strncpy(pak_name, at + 1, PAK_NAME_LEN - 1);
            pak_name[PAK_NAME_LEN-1] = '\0';
        } else {
            strncpy(src, argv[i], sizeof(src)-1); src[sizeof(src)-1]='\0';
            /* Use just the filename as PAK name if it's a file */
            const char *base = strrchr(src, '/');
            if (!base) base = strrchr(src, '\\');
            strncpy(pak_name, base ? base+1 : src, PAK_NAME_LEN-1);
            pak_name[PAK_NAME_LEN-1]='\0';
        }

        if (is_dir(src)) {
            /* For directories, walk and use relative paths */
            int before = a.count;
            /* Strip trailing slash */
            size_t slen = strlen(src);
            if (slen > 0 && (src[slen-1]=='/'||src[slen-1]=='\\'))
                src[slen-1] = '\0';
            walk_dir(&a, src, "");
            for (int j = before; j < a.count; j++)
                total_bytes += (long)a.entries[j].size;
            printf("  + %s/ (%d files)\n", src, a.count - before);
            total_files += a.count - before;
        } else {
            long sz = file_size(src);
            if (sz < 0) { warn("'%s' not found, skipping", src); continue; }
            archive_push(&a, pak_name, src, (uint32_t)sz);
            char szbuf[32]; fmt_size(sz, szbuf, sizeof(szbuf));
            printf("  + %-48s %s\n", pak_name, szbuf);
            total_files++;
            total_bytes += sz;
        }
    }

    if (total_files == 0) {
        fprintf(stderr, "waspakr: no files to pack\n");
        return 1;
    }

    printf("\nWriting %s...\n", output);
    pak_write(&a, output);

    long out_sz = file_size(output);
    char szbuf[32]; fmt_size(out_sz, szbuf, sizeof(szbuf));
    printf("Done. %d file(s), %s → %s\n", total_files, szbuf, output);

    /* Free */
    for (int i = 0; i < a.count; i++) free(a.entries[i].src_path);
    free(a.entries);
    return 0;
}

/* -------------------------------------------------------------------------
 * cmd_extract — extract all files from a PAK
 * Usage: waspakr extract <input.pak> [dest_dir]
 * -------------------------------------------------------------------------*/
static int cmd_extract(int argc, char *argv[]) {
    if (argc < 1) {
        fprintf(stderr, "usage: waspakr extract <input.pak> [dest_dir]\n");
        return 1;
    }

    const char *pak_path  = argv[0];
    const char *dest_root = (argc >= 2) ? argv[1] : ".";

    PakArchive *a = pak_open(pak_path);
    printf("Extracting %d file(s) from %s → %s/\n\n",
           a->count, pak_path, dest_root);

    long total_bytes = 0;
    int  extracted   = 0;

    for (int i = 0; i < a->count; i++) {
        PakEntry *e = &a->entries[i];

        /* Build destination path */
        char dest[1024];
        snprintf(dest, sizeof(dest), "%s/%s", dest_root, e->name);
        normalize_slashes(dest);

        /* Create parent directories */
        mkdirs(dest);

        /* Read and write */
        uint8_t *data = pak_read_entry(pak_path, e);

        FILE *out = fopen(dest, "wb");
        if (!out) {
            warn("cannot create '%s': %s", dest, strerror(errno));
            free(data);
            continue;
        }
        fwrite(data, 1, e->size, out);
        fclose(out);
        free(data);

        char szbuf[32]; fmt_size((long)e->size, szbuf, sizeof(szbuf));
        printf("  → %-52s %s\n", e->name, szbuf);
        total_bytes += (long)e->size;
        extracted++;
    }

    char szbuf[32]; fmt_size(total_bytes, szbuf, sizeof(szbuf));
    printf("\nExtracted %d/%d file(s), %s total.\n",
           extracted, a->count, szbuf);

    pak_free(a);
    return 0;
}

/* -------------------------------------------------------------------------
 * cmd_list — list PAK contents
 * Usage: waspakr list <input.pak> [-v]
 * -------------------------------------------------------------------------*/
static int cmd_list(int argc, char *argv[]) {
    if (argc < 1) {
        fprintf(stderr, "usage: waspakr list <input.pak> [-v]\n");
        return 1;
    }

    const char *pak_path = argv[0];
    bool verbose = (argc >= 2 && strcmp(argv[1], "-v") == 0);

    PakArchive *a = pak_open(pak_path);

    long total_bytes = 0;
    for (int i = 0; i < a->count; i++)
        total_bytes += (long)a->entries[i].size;

    char pak_szbuf[32]; fmt_size(file_size(pak_path), pak_szbuf, sizeof(pak_szbuf));
    char dat_szbuf[32]; fmt_size(total_bytes, dat_szbuf, sizeof(dat_szbuf));

    printf("PAK: %s  (%s on disk, %s data, %d entries)\n\n",
           pak_path, pak_szbuf, dat_szbuf, a->count);

    if (verbose) {
        printf("  %-4s  %-52s  %10s  %10s\n", "IDX", "NAME", "OFFSET", "SIZE");
        printf("  %-4s  %-52s  %10s  %10s\n",
               "----", "----------------------------------------------------",
               "----------", "----------");
        for (int i = 0; i < a->count; i++) {
            printf("  %-4d  %-52s  %10u  %10u\n",
                   i, a->entries[i].name,
                   a->entries[i].offset,
                   a->entries[i].size);
        }
    } else {
        for (int i = 0; i < a->count; i++) {
            char szbuf[32]; fmt_size((long)a->entries[i].size, szbuf, sizeof(szbuf));
            printf("  %-52s  %s\n", a->entries[i].name, szbuf);
        }
    }

    printf("\n%d file(s), %s uncompressed.\n", a->count, dat_szbuf);
    pak_free(a);
    return 0;
}

/* -------------------------------------------------------------------------
 * cmd_replace — replace a specific file inside a PAK
 * Usage: waspakr replace <input.pak> <pakname> <new_file>
 *
 * This is the key command for swapping out progs.dat without re-packing.
 * -------------------------------------------------------------------------*/
static int cmd_replace(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "usage: waspakr replace <input.pak> <pakname> <new_file>\n");
        fprintf(stderr, "  e.g. waspakr replace pak0.pak progs/progs.dat ./progs.dat\n");
        return 1;
    }

    const char *pak_path  = argv[0];
    const char *pak_name  = argv[1];
    const char *new_file  = argv[2];

    /* Normalise the target name for comparison */
    char target[PAK_NAME_LEN];
    strncpy(target, pak_name, PAK_NAME_LEN-1); target[PAK_NAME_LEN-1]='\0';
    normalize_slashes(target);
    to_lower(target);

    /* Read the replacement file */
    long new_size;
    uint8_t *new_data = read_file(new_file, &new_size);
    if (!new_data) die("cannot read '%s': %s", new_file, strerror(errno));

    /* Load the existing archive */
    PakArchive *a = pak_open(pak_path);

    /* Find the entry */
    int found = -1;
    for (int i = 0; i < a->count; i++) {
        char cmp[PAK_NAME_LEN];
        strncpy(cmp, a->entries[i].name, PAK_NAME_LEN-1); cmp[PAK_NAME_LEN-1]='\0';
        to_lower(cmp);
        if (strcmp(cmp, target) == 0) { found = i; break; }
    }

    if (found < 0) {
        /* Not found — offer to add instead */
        fprintf(stderr, "waspakr: '%s' not found in %s\n", pak_name, pak_path);
        fprintf(stderr, "  Use 'waspakr add' to add new files.\n");
        free(new_data); pak_free(a);
        return 1;
    }

    PakEntry *e = &a->entries[found];
    char old_szbuf[32], new_szbuf[32];
    fmt_size((long)e->size,  old_szbuf, sizeof(old_szbuf));
    fmt_size(new_size, new_szbuf, sizeof(new_szbuf));
    printf("Replacing: %s\n", e->name);
    printf("  old: %s\n  new: %s\n\n", old_szbuf, new_szbuf);

    /* Load all OTHER entries' data from the original PAK before rewriting */
    for (int i = 0; i < a->count; i++) {
        if (i == found) continue;
        a->entries[i].data = pak_read_entry(pak_path, &a->entries[i]);
    }

    /* Plug in the replacement */
    free(e->data);
    e->data = new_data;
    e->size = (uint32_t)new_size;

    /* Write back to the same file */
    pak_write(a, pak_path);

    char szbuf[32]; fmt_size(file_size(pak_path), szbuf, sizeof(szbuf));
    printf("Done. PAK updated: %s (%s)\n", pak_path, szbuf);

    pak_free(a);
    return 0;
}

/* -------------------------------------------------------------------------
 * cmd_add — add a new file to an existing PAK
 * Usage: waspakr add <input.pak> <file> [pakname]
 * -------------------------------------------------------------------------*/
static int cmd_add(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "usage: waspakr add <input.pak> <file> [pakname]\n");
        return 1;
    }

    const char *pak_path = argv[0];
    const char *src_file = argv[1];

    char pak_name[PAK_NAME_LEN];
    if (argc >= 3) {
        strncpy(pak_name, argv[2], PAK_NAME_LEN-1);
    } else {
        const char *base = strrchr(src_file, '/');
        if (!base) base = strrchr(src_file, '\\');
        strncpy(pak_name, base ? base+1 : src_file, PAK_NAME_LEN-1);
    }
    pak_name[PAK_NAME_LEN-1]='\0';
    normalize_slashes(pak_name);
    to_lower(pak_name);

    /* Check it doesn't already exist */
    PakArchive *a = pak_open(pak_path);
    for (int i = 0; i < a->count; i++) {
        char cmp[PAK_NAME_LEN];
        strncpy(cmp, a->entries[i].name, PAK_NAME_LEN-1); cmp[PAK_NAME_LEN-1]='\0';
        to_lower(cmp);
        if (strcmp(cmp, pak_name) == 0) {
            fprintf(stderr, "waspakr: '%s' already exists in PAK — use 'replace' instead\n", pak_name);
            pak_free(a); return 1;
        }
    }

    /* Read the new file */
    long new_size;
    uint8_t *new_data = read_file(src_file, &new_size);
    if (!new_data) die("cannot read '%s': %s", src_file, strerror(errno));

    /* Load all existing entries */
    for (int i = 0; i < a->count; i++)
        a->entries[i].data = pak_read_entry(pak_path, &a->entries[i]);

    /* Add new entry */
    archive_push(a, pak_name, NULL, (uint32_t)new_size);
    a->entries[a->count-1].data = new_data;

    pak_write(a, pak_path);

    char szbuf[32]; fmt_size(new_size, szbuf, sizeof(szbuf));
    printf("Added: %s (%s)\n", pak_name, szbuf);
    char pakszbuf[32]; fmt_size(file_size(pak_path), pakszbuf, sizeof(pakszbuf));
    printf("PAK updated: %s (%s, %d entries)\n", pak_path, pakszbuf, a->count);

    pak_free(a);
    return 0;
}

/* -------------------------------------------------------------------------
 * cmd_remove — remove a file from a PAK
 * Usage: waspakr remove <input.pak> <pakname>
 * -------------------------------------------------------------------------*/
static int cmd_remove(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "usage: waspakr remove <input.pak> <pakname>\n");
        return 1;
    }

    const char *pak_path = argv[0];
    char target[PAK_NAME_LEN];
    strncpy(target, argv[1], PAK_NAME_LEN-1); target[PAK_NAME_LEN-1]='\0';
    normalize_slashes(target); to_lower(target);

    PakArchive *a = pak_open(pak_path);

    int found = -1;
    for (int i = 0; i < a->count; i++) {
        char cmp[PAK_NAME_LEN];
        strncpy(cmp, a->entries[i].name, PAK_NAME_LEN-1); cmp[PAK_NAME_LEN-1]='\0';
        to_lower(cmp);
        if (strcmp(cmp, target) == 0) { found = i; break; }
    }

    if (found < 0) {
        fprintf(stderr, "waspakr: '%s' not found in %s\n", argv[1], pak_path);
        pak_free(a); return 1;
    }

    printf("Removing: %s (%u bytes)\n", a->entries[found].name, a->entries[found].size);

    /* Load all other entries */
    for (int i = 0; i < a->count; i++) {
        if (i == found) continue;
        a->entries[i].data = pak_read_entry(pak_path, &a->entries[i]);
    }

    /* Shift entries */
    free(a->entries[found].src_path);
    for (int i = found; i < a->count - 1; i++)
        a->entries[i] = a->entries[i+1];
    a->count--;

    pak_write(a, pak_path);

    char szbuf[32]; fmt_size(file_size(pak_path), szbuf, sizeof(szbuf));
    printf("Done. PAK updated: %s (%s, %d entries)\n", pak_path, szbuf, a->count);

    pak_free(a);
    return 0;
}

/* -------------------------------------------------------------------------
 * cmd_check — validate PAK integrity
 * Usage: waspakr check <input.pak>
 * -------------------------------------------------------------------------*/
static int cmd_check(int argc, char *argv[]) {
    if (argc < 1) {
        fprintf(stderr, "usage: waspakr check <input.pak>\n");
        return 1;
    }

    const char *pak_path = argv[0];
    long pak_size = file_size(pak_path);
    if (pak_size < 0) die("cannot stat '%s'", pak_path);

    FILE *f = fopen(pak_path, "rb");
    if (!f) die("cannot open '%s'", pak_path);

    pak_header_t hdr;
    if (fread(&hdr, sizeof(hdr), 1, f) != 1) { fclose(f); die("truncated header"); }

    printf("Checking: %s\n\n", pak_path);

    int issues = 0;

    /* Magic */
    if (memcmp(hdr.magic, PAK_MAGIC, 4) != 0) {
        printf("  [FAIL] Bad magic bytes (not a PAK file)\n");
        fclose(f); return 1;
    }
    printf("  [OK]   Magic: PACK\n");

    /* Directory bounds */
    if ((long)(hdr.dir_offset + hdr.dir_size) > pak_size) {
        printf("  [FAIL] Directory extends beyond end of file\n");
        issues++;
    } else {
        printf("  [OK]   Directory offset: %u, size: %u\n",
               hdr.dir_offset, hdr.dir_size);
    }

    uint32_t num = hdr.dir_size / PAK_ENTRY_SIZE;
    printf("  [OK]   Entries: %u\n", num);

    /* Read directory */
    fseek(f, hdr.dir_offset, SEEK_SET);
    pak_entry_t *raw = malloc(num * sizeof(pak_entry_t));
    if (!raw) die("out of memory");
    if (fread(raw, sizeof(pak_entry_t), num, f) != num) { free(raw); fclose(f); die("failed to read directory"); }

    int overlaps = 0;
    for (uint32_t i = 0; i < num; i++) {
        uint32_t start = raw[i].offset;
        uint32_t end   = raw[i].offset + raw[i].size;

        /* Check against file bounds */
        if ((long)end > pak_size) {
            printf("  [FAIL] Entry %-40s exceeds file size\n", raw[i].name);
            issues++;
        }

        /* Check for overlaps with other entries */
        for (uint32_t j = i+1; j < num; j++) {
            uint32_t s2 = raw[j].offset, e2 = raw[j].offset + raw[j].size;
            if (start < e2 && end > s2) {
                printf("  [WARN] Entries '%s' and '%s' overlap\n",
                       raw[i].name, raw[j].name);
                overlaps++;
            }
        }

        /* Check null termination in name */
        bool null_found = false;
        for (int k = 0; k < PAK_NAME_LEN; k++) {
            if (raw[i].name[k] == '\0') { null_found = true; break; }
        }
        if (!null_found) {
            printf("  [WARN] Entry %u: name not null-terminated\n", i);
            issues++;
        }
    }

    free(raw);
    fclose(f);

    if (overlaps == 0 && issues == 0) {
        char szbuf[32]; fmt_size(pak_size, szbuf, sizeof(szbuf));
        printf("\n  All checks passed. %u entries, %s\n", num, szbuf);
        return 0;
    } else {
        printf("\n  %d issue(s) found.\n", issues + overlaps);
        return 1;
    }
}

/* -------------------------------------------------------------------------
 * Banner & usage
 * -------------------------------------------------------------------------*/
static void print_banner(void) {
    fprintf(stderr,
        "\n"
        "  db   d8b   db  .d8b.  .d8888. d8888b.  .d8b.  db   dD d8888b.\n"
        "  88   I8I   88 d8' `8b 88'  YP 88  `8D d8' `8b 88 ,8P' 88  `8D\n"
        "  88   I8I   88 88ooo88 `8bo.   88oodD' 88ooo88 88,8P   88oobY'\n"
        "  Y8   I8I   88 88~~~88   `Y8b. 88~~~   88~~~88 88`8b   88`8b\n"
        "   `8b d8'8b d8' 88   88 db   8D 88      88   88 88 `88. 88 `88.\n"
        "    `8b8' `8d8'  YP   YP `8888Y' 88      YP   YP YP   YD 88   YD\n"
        "\n"
        "  WasPakr v%s — Quake PAK Archiver\n\n",
        WASPAKR_VERSION);
}

static void print_usage(void) {
    fprintf(stderr,
        "  Usage:\n"
        "    waspakr pack    <output.pak> <dir|file[@name]...>  Create PAK\n"
        "    waspakr extract <input.pak>  [dest_dir]            Extract all\n"
        "    waspakr list    <input.pak>  [-v]                  List contents\n"
        "    waspakr replace <input.pak>  <pakname> <file>      Replace file\n"
        "    waspakr add     <input.pak>  <file>    [pakname]   Add file\n"
        "    waspakr remove  <input.pak>  <pakname>             Remove file\n"
        "    waspakr check   <input.pak>                        Validate PAK\n"
        "\n"
        "  Examples:\n"
        "    waspakr pack    pak0.pak ./id1/                    Pack entire dir\n"
        "    waspakr extract pak0.pak ./extracted/              Extract to dir\n"
        "    waspakr list    pak0.pak -v                        Verbose listing\n"
        "    waspakr replace pak0.pak progs/progs.dat ./progs.dat  Swap progs.dat\n"
        "    waspakr check   pak0.pak                           Verify integrity\n"
        "\n"
        "  Aliases: p=pack  x=extract  l=list  r=replace  a=add  d=remove  c=check\n\n"
    );
}

/* -------------------------------------------------------------------------
 * main
 * -------------------------------------------------------------------------*/
int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_banner();
        print_usage();
        return 1;
    }

    const char *cmd = argv[1];

    /* Version / help shortcuts */
    if (strcmp(cmd, "--version") == 0 || strcmp(cmd, "-V") == 0) {
        printf("WasPakr v%s\n", WASPAKR_VERSION);
        return 0;
    }
    if (strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0 || strcmp(cmd, "help") == 0) {
        print_banner();
        print_usage();
        return 0;
    }

    /* Dispatch — support both full names and single-char aliases */
    int sub_argc = argc - 2;
    char **sub_argv = argv + 2;

    if (strcmp(cmd,"pack")   ==0||strcmp(cmd,"p")==0) return cmd_pack(sub_argc,   sub_argv);
    if (strcmp(cmd,"extract")==0||strcmp(cmd,"x")==0) return cmd_extract(sub_argc,sub_argv);
    if (strcmp(cmd,"list")   ==0||strcmp(cmd,"l")==0) return cmd_list(sub_argc,   sub_argv);
    if (strcmp(cmd,"replace")==0||strcmp(cmd,"r")==0) return cmd_replace(sub_argc,sub_argv);
    if (strcmp(cmd,"add")    ==0||strcmp(cmd,"a")==0) return cmd_add(sub_argc,    sub_argv);
    if (strcmp(cmd,"remove") ==0||strcmp(cmd,"d")==0) return cmd_remove(sub_argc, sub_argv);
    if (strcmp(cmd,"check")  ==0||strcmp(cmd,"c")==0) return cmd_check(sub_argc,  sub_argv);

    print_banner();
    fprintf(stderr, "  Unknown command '%s'\n\n", cmd);
    print_usage();
    return 1;
}
