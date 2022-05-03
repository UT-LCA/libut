/*
 * cfg.c - configuration file support
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

#include <base/stddef.h>
#include <base/bitmap.h>
#include <base/log.h>
#include <base/cpu.h>

#include "defs.h"

struct net_cfg netcfg __aligned(CACHE_LINE_SIZE);

/*
 * Configuration Options
 */

static int str_to_long(const char *str, long *val)
{
    char *endptr;
    *val = strtol(str, &endptr, 10);
    if (endptr == str || (*endptr != '\0' && *endptr != '\n') ||
        ((*val == LONG_MIN || *val == LONG_MAX) && errno == ERANGE))
        return -EINVAL;
    return 0;
}

static int parse_runtime_kthreads(const char *name, const char *val)
{
    long tmp;
    int ret;

    ret = str_to_long(val, &tmp);
    if (ret)
        return ret;

    if (tmp < 1 || tmp > cpu_count - 1) {
        log_err("invalid number of kthreads requested, '%ld'", tmp);
        log_err("must be > 0 and < %d (number of CPUs)", cpu_count);
        return -EINVAL;
    }

    maxks = tmp;
    return 0;
}

static int parse_runtime_spinning_kthreads(const char *name, const char *val)
{
    long tmp;
    int ret;

    ret = str_to_long(val, &tmp);
    if (ret)
        return ret;

    if (tmp < 0) {
        log_err("invalid number of spinning kthreads requests, '%ld', "
            "must be > 0", tmp);
        return -EINVAL;
    }

    spinks = tmp;
    return 0;
}

static int parse_runtime_guaranteed_kthreads(const char *name, const char *val)
{
    long tmp;
    int ret;

    ret = str_to_long(val, &tmp);
    if (ret)
        return ret;

    if (tmp > cpu_count - 1) {
        log_err("invalid number of guaranteed kthreads requested, '%ld'", tmp);
        log_err("must be < %d (number of CPUs)", cpu_count);
        return -EINVAL;
    } else if (tmp < 1) {
        log_warn("< 1 guaranteed kthreads is not recommended for networked apps");
    }

    guaranteedks = tmp;
    return 0;
}

static int parse_watchdog_flag(const char *name, const char *val)
{
    disable_watchdog = true;
    return 0;
}

static int parse_log_level(const char *name, const char *val)
{
    long tmp;
    int ret;

    ret = str_to_long(val, &tmp);
    if (ret)
        return ret;

    if (tmp < LOG_EMERG || tmp > LOG_DEBUG) {
        log_err("log level must be between %d and %d",
            LOG_EMERG, LOG_DEBUG);
        return -EINVAL;
    }

    max_loglevel = tmp;
    return 0;
}


/*
 * Parsing Infrastructure
 */

typedef int (*cfg_fn_t)(const char *name, const char *val);

struct cfg_handler {
    const char    *name;
    cfg_fn_t       fn;
    bool           required;
};

static const struct cfg_handler cfg_handlers[] = {
    { "runtime_kthreads", parse_runtime_kthreads, true },
    { "runtime_spinning_kthreads", parse_runtime_spinning_kthreads, false },
    { "runtime_guaranteed_kthreads", parse_runtime_guaranteed_kthreads,
            false },
    { "log_level", parse_log_level, false },
    { "disable_watchdog", parse_watchdog_flag, false },
};

/**
 * cfg_load - loads the configuration file
 * @path: a path to the configuration file
 *
 * Returns 0 if successful, otherwise fail.
 */
int cfg_load(const char *path)
{
    FILE *f;
    char buf[BUFSIZ];
    DEFINE_BITMAP(parsed, ARRAY_SIZE(cfg_handlers));
    const char *name, *val;
    int i, ret = 0, line = 0;

    bitmap_init(parsed, ARRAY_SIZE(cfg_handlers), 0);

    log_info("loading configuration from '%s'", path);

    f = fopen(path, "r");
    if (!f)
        return -errno;

    while (fgets(buf, sizeof(buf), f)) {
        if (buf[0] == '#' || buf[0] == '\n') {
            line++;
            continue;
        }
        name = strtok(buf, " ");
        if (!name)
            break;
        val = strtok(NULL, " ");

        for (i = 0; i < ARRAY_SIZE(cfg_handlers); i++) {
            const struct cfg_handler *h = &cfg_handlers[i];
            if (!strncmp(name, h->name, BUFSIZ)) {
                ret = h->fn(name, val);
                if (ret) {
                    log_err("bad config option on line %d",
                        line);
                    goto out;
                }
                bitmap_set(parsed, i);
                break;
            }
        }

        line++;
    }

    for (i = 0; i < ARRAY_SIZE(cfg_handlers); i++) {
        const struct cfg_handler *h = &cfg_handlers[i];
        if (h->required && !bitmap_test(parsed, i)) {
            log_err("missing required config option '%s'", h->name);
            ret = -EINVAL;
            goto out;
        }
    }

    if (guaranteedks > maxks) {
        log_err("invalid number of guaranteed kthreads requested, '%d'",
                guaranteedks);
        log_err("must be <= %d (number of kthreads)", maxks);
        ret = -EINVAL;
        goto out;
    }

out:
    fclose(f);
    return ret;
}
