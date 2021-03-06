/*	$Id$ */
/*
 * Copyright (c) 2018 Kristaps Dzonsons <kristaps@bsd.lv>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <arpa/inet.h>

#include <assert.h>
#include <curses.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "extern.h"
#include "slant.h"

struct	parse {
	const char	 *fn; /* current parse function */
	const char	**toks; /* list of all tokens */
	size_t		  toksz; /* number of tokens */
	size_t		  pos; /* position in tokens */
};

static int
tok_unknown(const struct parse *p)
{

	warnx("%s: unknown token: \"%s\"", 
		p->fn, p->toks[p->pos]);
	return 0;
}

/*
 * Check whether the current token exists, i.e., whether we haven't
 * fallen into the EOF.
 * Returns zero on (and logs the) failure (EOF), non-zero otherwise.
 */
static int
tok_nadv(const struct parse *p)
{

	if (p->pos >= p->toksz) {
		warnx("%s: unexpected eof", p->fn);
		return 0;
	}
	return 1;
}

/*
 * Expect a given token "v" at the current parse point.
 * Check if that's the case.
 * Returns zero (and logs) if it is not; non-zero otherwise.
 */
static int
tok_expect(const struct parse *p, const char *v)
{

	if ( ! tok_nadv(p))
		return 0;
	if (0 == strcmp(p->toks[p->pos], v))
		return 1;
	warnx("%s: expected \"%s\", have \"%s\"", 
		p->fn, v, p->toks[p->pos]);
	return 0;
}

/*
 * Just tok_expect(), but also advancing on success.
 */
static int
tok_expect_adv(struct parse *p, const char *v)
{

	if (tok_expect(p, v)) {
		p->pos++;
		return 1;
	}
	return 0;
}

/*
 * Test whether the current token equals "v".
 * Returns zero on failure (no match), non-zero on success.
 */
static int
tok_eq(const struct parse *p, const char *v)
{

	assert(p->pos < p->toksz);
	return 0 == strcmp(p->toks[p->pos], v);
}

/*
 * Like tok_eq(), but advanced on success.
 */
static int
tok_eq_adv(struct parse *p, const char *v)
{

	assert(p->pos < p->toksz);
	if (0 == strcmp(p->toks[p->pos], v)) {
		p->pos++;
		return 1;
	}
	return 0;
}

/*
 * Advance to the next position then call tok_nadv().
 * See tok_nadv() for return values and details.
 */
static int
tok_adv(struct parse *p)
{

	++p->pos;
	return tok_nadv(p);
}

/*
 * "waittime" num ";"
 */
static int
parse_waittime(struct parse *p, struct config *cfg)
{
	const char	*er;

	assert(p->pos < p->toksz);
	cfg->waittime = strtonum
		(p->toks[p->pos], 15, INT_MAX, &er);
	if (NULL != er) {
		warnx("%s: bad global waittime: %s", p->fn, er);
		return 0;
	} else if ( ! tok_adv(p)) {
		return 0;
	} else if ( ! tok_expect_adv(p, ";"))
		return 0;

	return 1;
}

/*
 * ["waittime" num] "}"
 */
static int
parse_server_args(struct parse *p, struct config *cfg, size_t count)
{
	const char	*er;
	time_t		 waittime = 0;
	size_t		 i;

	while (p->pos < p->toksz && ! tok_eq(p, "}")) {
		if (tok_eq_adv(p, "waittime")) {
			waittime = strtonum
				(p->toks[p->pos], 15, INT_MAX, &er);
			if (NULL != er) {
				warnx("%s: bad server waittime: "
					"%s", p->fn, er);
				return 0;
			}
			if ( ! tok_adv(p))
				return 0;
			(void)tok_eq_adv(p, ";");
		} else
			return tok_unknown(p);
	}

	if ( ! tok_expect_adv(p, "}"))
		return 0;

	assert(cfg->urlsz >= count);
	if (0 == waittime)
		return 1;

	/* Apply waittime to all previous hosts. */

	for (i = 0; i < count; i++)
		cfg->urls[cfg->urlsz - 1 - i].waittime = waittime;

	return 1;
}

static int
parse_layout_host(struct parse *p, struct config *cfg)
{
	void		*pp;
	struct drawbox	*b;

	if ( ! tok_expect_adv(p, "{"))
		return 0;
	if (tok_eq(p, "}"))
		return 1;

	while (p->pos < p->toksz) {
		pp = reallocarray(cfg->draw->box,
			cfg->draw->boxsz + 1,
			sizeof(struct drawbox));
		if (NULL == pp) {
			warn(NULL);
			return 0;
		}

		cfg->draw->box = pp;
		b = &cfg->draw->box[cfg->draw->boxsz++];
		memset(b, 0, sizeof(struct drawbox));

		if (tok_eq_adv(p, "cpu")) {
			b->cat = DRAWCAT_CPU;
			while (p->pos < p->toksz)
				if (tok_eq_adv(p, "qmin_bars"))
					b->args |= CPU_QMIN_BARS;
				else if (tok_eq_adv(p, "qmin"))
					b->args |= CPU_QMIN;
				else if (tok_eq_adv(p, "min"))
					b->args |= CPU_MIN;
				else if (tok_eq_adv(p, "hour"))
					b->args |= CPU_HOUR;
				else if (tok_eq_adv(p, "day"))
					b->args |= CPU_DAY;
				else if (tok_eq_adv(p, "week"))
					b->args |= CPU_WEEK;
				else if (tok_eq_adv(p, "year"))
					b->args |= CPU_YEAR;
				else if (tok_eq(p, ";"))
					break;
				else if (tok_eq(p, "}"))
					break;
				else
					return tok_unknown(p);
		} else if (tok_eq_adv(p, "mem")) {
			b->cat = DRAWCAT_MEM;
			while (p->pos < p->toksz)
				if (tok_eq_adv(p, "qmin_bars"))
					b->args |= MEM_QMIN_BARS;
				else if (tok_eq_adv(p, "qmin"))
					b->args |= MEM_QMIN;
				else if (tok_eq_adv(p, "min"))
					b->args |= MEM_MIN;
				else if (tok_eq_adv(p, "hour"))
					b->args |= MEM_HOUR;
				else if (tok_eq_adv(p, "day"))
					b->args |= MEM_DAY;
				else if (tok_eq_adv(p, "week"))
					b->args |= MEM_WEEK;
				else if (tok_eq_adv(p, "year"))
					b->args |= MEM_YEAR;
				else if (tok_eq(p, ";"))
					break;
				else if (tok_eq(p, "}"))
					break;
				else
					return tok_unknown(p);
		} else if (tok_eq_adv(p, "net")) {
			b->cat = DRAWCAT_NET;
			while (p->pos < p->toksz)
				if (tok_eq_adv(p, "qmin"))
					b->args |= NET_QMIN;
				else if (tok_eq_adv(p, "min"))
					b->args |= NET_MIN;
				else if (tok_eq_adv(p, "hour"))
					b->args |= NET_HOUR;
				else if (tok_eq_adv(p, "day"))
					b->args |= NET_DAY;
				else if (tok_eq_adv(p, "week"))
					b->args |= NET_WEEK;
				else if (tok_eq_adv(p, "year"))
					b->args |= NET_YEAR;
				else if (tok_eq(p, ";"))
					break;
				else if (tok_eq(p, "}"))
					break;
				else
					return tok_unknown(p);
		} else if (tok_eq_adv(p, "disc")) {
			b->cat = DRAWCAT_DISC;
			while (p->pos < p->toksz)
				if (tok_eq_adv(p, "qmin"))
					b->args |= DISC_QMIN;
				else if (tok_eq_adv(p, "min"))
					b->args |= DISC_MIN;
				else if (tok_eq_adv(p, "hour"))
					b->args |= DISC_HOUR;
				else if (tok_eq_adv(p, "day"))
					b->args |= DISC_DAY;
				else if (tok_eq_adv(p, "week"))
					b->args |= DISC_WEEK;
				else if (tok_eq_adv(p, "year"))
					b->args |= DISC_YEAR;
				else if (tok_eq(p, ";"))
					break;
				else if (tok_eq(p, "}"))
					break;
				else
					return tok_unknown(p);
		} else if (tok_eq_adv(p, "link")) {
			b->cat = DRAWCAT_LINK;
			while (p->pos < p->toksz)
				if (tok_eq_adv(p, "ip"))
					b->args |= LINK_IP;
				else if (tok_eq_adv(p, "state"))
					b->args |= LINK_STATE;
				else if (tok_eq_adv(p, "access"))
					b->args |= LINK_ACCESS;
				else if (tok_eq(p, ";"))
					break;
				else if (tok_eq(p, "}"))
					break;
				else
					return tok_unknown(p);
		} else if (tok_eq_adv(p, "host")) {
			b->cat = DRAWCAT_HOST;
			b->args = HOST_ACCESS;
			while (p->pos < p->toksz)
				if (tok_eq(p, ";"))
					break;
				else if (tok_eq(p, "}"))
					break;
				else
					return tok_unknown(p);
		} else if (tok_eq_adv(p, "nprocs")) {
			b->cat = DRAWCAT_PROCS;
			while (p->pos < p->toksz)
				if (tok_eq_adv(p, "qmin_bars"))
					b->args |= PROCS_QMIN_BARS;
				else if (tok_eq_adv(p, "qmin"))
					b->args |= PROCS_QMIN;
				else if (tok_eq_adv(p, "min"))
					b->args |= PROCS_MIN;
				else if (tok_eq_adv(p, "hour"))
					b->args |= PROCS_HOUR;
				else if (tok_eq_adv(p, "day"))
					b->args |= PROCS_DAY;
				else if (tok_eq_adv(p, "week"))
					b->args |= PROCS_WEEK;
				else if (tok_eq_adv(p, "year"))
					b->args |= PROCS_YEAR;
				else if (tok_eq(p, ";"))
					break;
				else if (tok_eq(p, "}"))
					break;
				else
					return tok_unknown(p);
		} else if (tok_eq_adv(p, "rprocs")) {
			b->cat = DRAWCAT_RPROCS;
			while (p->pos < p->toksz)
				if (tok_eq_adv(p, "qmin"))
					b->args |= RPROCS_QMIN;
				else if (tok_eq_adv(p, "qmin_bars"))
					b->args |= RPROCS_QMIN_BARS;
				else if (tok_eq_adv(p, "min"))
					b->args |= RPROCS_MIN;
				else if (tok_eq_adv(p, "hour"))
					b->args |= RPROCS_HOUR;
				else if (tok_eq_adv(p, "day"))
					b->args |= RPROCS_DAY;
				else if (tok_eq_adv(p, "week"))
					b->args |= RPROCS_WEEK;
				else if (tok_eq_adv(p, "year"))
					b->args |= RPROCS_YEAR;
				else if (tok_eq(p, ";"))
					break;
				else if (tok_eq(p, "}"))
					break;
				else
					return tok_unknown(p);
		} else if (tok_eq_adv(p, "nfiles")) {
			b->cat = DRAWCAT_FILES;
			while (p->pos < p->toksz)
				if (tok_eq_adv(p, "qmin"))
					b->args |= FILES_QMIN;
				else if (tok_eq_adv(p, "qmin_bars"))
					b->args |= FILES_QMIN_BARS;
				else if (tok_eq_adv(p, "min"))
					b->args |= FILES_MIN;
				else if (tok_eq_adv(p, "hour"))
					b->args |= FILES_HOUR;
				else if (tok_eq_adv(p, "day"))
					b->args |= FILES_DAY;
				else if (tok_eq_adv(p, "week"))
					b->args |= FILES_WEEK;
				else if (tok_eq_adv(p, "year"))
					b->args |= FILES_YEAR;
				else if (tok_eq(p, ";"))
					break;
				else if (tok_eq(p, "}"))
					break;
				else
					return tok_unknown(p);
		} else
			return tok_unknown(p);

		if (tok_eq(p, "}"))
			break;
		if ( ! tok_expect_adv(p, ";"))
			return 0;
		if (tok_eq(p, "}"))
			break;
	}

	if ( ! tok_expect_adv(p, "}"))
		return 0;

	return 1;
}

/*
 * Parse a layout statement.
 * Return zero on success, non-zero otherwise.
 * Fills in the cfg->draw, possibly on failure.
 */
static int
parse_layout(struct parse *p, struct config *cfg)
{
	const char	*er;

	if ( ! tok_expect_adv(p, "{"))
		return 0;
	if (tok_eq_adv(p, "}")) 
		return 1;

	if (NULL != cfg->draw) {
		warnx("%s: layout already specified", p->fn);
		return 0;
	}
	if (NULL == (cfg->draw = calloc(1, sizeof(struct draw)))) {
		warn(NULL);
		return 0;
	}

	while (p->pos < p->toksz) {
		if (tok_eq_adv(p, "header")) {
			cfg->draw->header = 1;
		} else if (tok_eq_adv(p, "errlog")) {
			cfg->draw->errlog = strtonum
				(p->toks[p->pos], 0, INT_MAX, &er);
			if (NULL != er) {
				warnx("%s: bad layout errlog: "
					"%s", p->fn, er);
				return 0;
			}
			p->pos++;
		} else if (tok_eq_adv(p, "host")) {
			if ( ! parse_layout_host(p, cfg))
				return 0;
		} else 
			return tok_unknown(p);

		if (tok_eq(p, "}"))
			break;
		if ( ! tok_expect_adv(p, ";"))
			return 0;
		if (tok_eq(p, "}"))
			break;
	}

	if ( ! tok_expect_adv(p, "}"))
		return 0;
	return tok_expect_adv(p, ";");
}

/*
 * "servers" s1 [s2...] ["{" args] ";"
 */
static int
parse_servers(struct parse *p, struct config *cfg)
{
	void	*pp;
	size_t	 count = 0;

	while (p->pos < p->toksz) {
		if (tok_eq(p, ";") || tok_eq(p, "{"))
			break;
		pp = reallocarray
			(cfg->urls, cfg->urlsz + 1,
			 sizeof(struct nconfig));
		if (NULL == pp) {
			warn(NULL);
			return 0;
		}
		cfg->urls = pp;
		memset(&cfg->urls[cfg->urlsz], 
			0, sizeof(struct nconfig));
		cfg->urlsz++;
		cfg->urls[cfg->urlsz - 1].url = 
			strdup(p->toks[p->pos]);
		if (NULL == cfg->urls[cfg->urlsz - 1].url) {
			warn(NULL);
			return 0;
		}
		p->pos++;
		count++;
	}

	if (0 == count) {
		warnx("%s: no servers in statement", p->fn);
		return 0;
	} else if ( ! tok_nadv(p)) 
		return 0;

	/* Now the arguments. */

	if (tok_eq_adv(p, "{")) {
		if ( ! parse_server_args(p, cfg, count))
			return 0;
		if ( ! tok_expect(p, ";"))
			return 0;
	}

	p->pos++;
	return 1;
}

static int
config_cmdline(struct config *cfg, int argc, char *argv[])
{
	size_t	 i;

	assert(argc);

	if (0 == argc)
		return 1;

	cfg->urlsz = argc;
	cfg->urls = calloc(cfg->urlsz, sizeof(struct nconfig));

	if (NULL == cfg->urls) {
		warn(NULL);
		return 0;
	}

	for (i = 0; i < cfg->urlsz; i++) {
		cfg->urls[i].url = strdup(argv[i]);
		if (NULL == cfg->urls[i].url) {
			warn(NULL);
			return 0;
		}
	}

	return 1;
}

int
config_parse(const char *fn, struct config *cfg,
	int argc, char *argv[])
{
	int		  fd;
	void		 *map, *pp;
	char		 *buf, *bufsv, *cp;
	char		**toks = NULL;
	size_t		  mapsz, i;
	struct stat	  st;
	struct parse	  p;

	memset(cfg, 0, sizeof(struct config));
	memset(&p, 0, sizeof(struct parse));

	/* Set some defaults. */

	cfg->waittime = 60;

	/* Open file, map it, create a NUL-terminated string. */

	if (-1 == (fd = open(fn, O_RDONLY, 0))) {
		if (ENOENT == errno)
			return config_cmdline(cfg, argc, argv);
		warn("%s", fn);
		return 0;
	} else if (-1 == fstat(fd, &st)) {
		warn("%s", fn);
		close(fd);
		return 0;
	}
	
	mapsz = (size_t)st.st_size;
	if (NULL == (buf = bufsv = malloc(mapsz + 1))) {
		warn(NULL);
		close(fd);
		return 0;
	}
	map = mmap(NULL, mapsz, PROT_READ, MAP_SHARED, fd, 0);
	if (MAP_FAILED == map) {
		warn("%s", fn);
		free(buf);
		close(fd);
		return 0;
	}

	memcpy(buf, map, mapsz);
	buf[mapsz] = '\0';
	munmap(map, mapsz);
	close(fd);

	/* 
	 * Step through all space-separated tokens.
	 * TODO: make this into a proper parsing sequence at some point,
	 * but for now this will do.
	 */

	while (NULL != (cp = strsep(&buf, " \t\r\n"))) {
		if ('\0' == *cp)
			continue;
		pp = reallocarray
			(toks, p.toksz + 1,
			 sizeof(char *));
		if (NULL == pp) {
			free(bufsv);
			free(toks);
			warn(NULL);
			return 0;
		}
		toks = pp;
		toks[p.toksz++] = cp;
	}

	p.toks = (const char **)toks;
	p.fn = fn;

	/* Recursive descent parse top-level driver. */

	while (p.pos < p.toksz)
		if (tok_eq_adv(&p, "servers")) {
			if ( ! parse_servers(&p, cfg))
				break;
		} else if (tok_eq_adv(&p, "layout")) {
			if ( ! parse_layout(&p, cfg))
				break;
		} else if (tok_eq_adv(&p, "waittime")) {
			if ( ! parse_waittime(&p, cfg))
				break;
		} else {
			tok_unknown(&p);
			break;
		}

	free(toks);
	free(bufsv);
	if (p.pos != p.toksz)
		return 0;

	if (0 == argc)
		return 1;

	/* Use only our command line entries. */

	for (i = 0; i < cfg->urlsz; i++)
		free(cfg->urls[i].url);

	free(cfg->urls);
	cfg->urls = NULL;
	cfg->urlsz = 0;

	return config_cmdline(cfg, argc, argv);
}

void
config_free(struct config *cfg)
{
	size_t	 i;

	if (NULL == cfg)
		return;

	for (i = 0; i < cfg->urlsz; i++)
		free(cfg->urls[i].url);
	if (NULL != cfg->draw)
		free(cfg->draw->box);

	free(cfg->draw);
	free(cfg->urls);
}
