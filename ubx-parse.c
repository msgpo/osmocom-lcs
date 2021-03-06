/*
 * ubx-parse.c
 *
 * Implementation of parsing code converting UBX messages to GPS assist
 * data
 *
 *
 * Copyright (C) 2009  Sylvain Munaut <tnt@246tNt.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>

#include "gps.h"
#include "ubx.h"
#include "ubx-parse.h"

#define DEBUG 1
#if DEBUG
	#define printd(x, args ...) printf(x, ## args)
#else
	#define printd(x, args ...)
#endif

#define DEBUG1 0
#if DEBUG1
	#define printd1(x, args ...) printf(x, ## args)
#else
	#define printd1(x, args ...)
#endif

/* Helpers */

static int
float_to_fixedpoint(float f, int sf)
{
	if (sf < 0) {
		while (sf++ < 0)
			f *= 2.0f;
	} else {
		while (sf-- > 0)
			f *= 0.5f;
	}

	return (int)f;
}

static inline int
double_to_fixedpoint(double d, int sf)
{
	if (sf < 0) {
		while (sf++ < 0)
			d *= 2.0;
	} else {
		while (sf-- > 0)
			d *= 0.5;
	}

	return (int)d;
}


/* UBX message parsing to fill gps assist data */

static void
_ubx_msg_parse_nav_posllh(struct ubx_hdr *hdr, void *pl, int pl_len, void *ud)
{
	struct ubx_nav_posllh *nav_posllh = pl;
	struct gps_assist_data *gps = ud;

	printd("[.] NAV_POSLLH\n");

	gps->fields |= GPS_FIELD_REFPOS;

	gps->ref_pos.latitude  = (double)(nav_posllh->lat) * 1e-7;
	gps->ref_pos.longitude = (double)(nav_posllh->lon) * 1e-7;
	gps->ref_pos.altitude  = (double)(nav_posllh->height) * 1e-3;
	
	printd("  TOW       %lu\n", nav_posllh->itow);
	printd("  latitude  %f\n", gps->ref_pos.latitude);
	printd("  longitude %f\n", gps->ref_pos.longitude);
	printd("  altitude  %f\n", gps->ref_pos.altitude);
}

static void
_ubx_msg_parse_aid_ini(struct ubx_hdr *hdr, void *pl, int pl_len, void *ud)
{
	struct ubx_aid_ini *aid_ini = pl;
	struct gps_assist_data *gps = ud;

	printd("[.] AID_INI\n");

	/* Extract info for "Reference Time" */
	gps->fields |= GPS_FIELD_REFTIME;

	gps->ref_time.wn = aid_ini->wn;
	gps->ref_time.tow = (double)aid_ini->tow * 1e-3;
	gps->ref_time.when = time(NULL);
	
	printd("  WN   %d\n", gps->ref_time.wn);
	printd("  TOW  %ld\n", aid_ini->tow);
		
	if((aid_ini->flags & 0x03) != 0x03) { /* time and pos valid ? */
		fprintf(stderr, "Postion and/or time not valid (0x%lx)", aid_ini->flags);
	}
	
	// FIXME: We could extract ref position as well but we need it in
	//        WGS84 geodetic coordinates and it's provided as ecef, so
	//        we need a lot of math ...
}

static void
_ubx_msg_parse_aid_hui(struct ubx_hdr *hdr, void *pl, int pl_len, void *ud)
{
	struct ubx_aid_hui *aid_hui = pl;
	struct gps_assist_data *gps = ud;

	printd("[.] AID_HUI\n");

	if (aid_hui->flags & 0x2) { /* UTC parameters valid */
		struct gps_utc_model *utc = &gps->utc;

		printd("  UTC\n");
		
		gps->fields |= GPS_FIELD_UTC;

		utc->a0          = double_to_fixedpoint(aid_hui->utc_a0, -30);
		utc->a1          = double_to_fixedpoint(aid_hui->utc_a1, -50);
		utc->delta_t_ls  = aid_hui->utc_ls;
		utc->t_ot        = aid_hui->utc_tot >> 12;
		utc->wn_t        = aid_hui->utc_wnt;
		utc->wn_lsf      = aid_hui->utc_wnf;
		utc->dn          = aid_hui->utc_dn;
		utc->delta_t_lsf = aid_hui->utc_lsf;
	}

	if (aid_hui->flags & 0x04) { /* Klobuchar parameters valid */
		struct gps_ionosphere_model *iono = &gps->ionosphere;

		printd("  IONOSPHERE\n");
		
		gps->fields |= GPS_FIELD_IONOSPHERE;

		iono->alpha_0 = float_to_fixedpoint(aid_hui->klob_a0, -30);
		iono->alpha_1 = float_to_fixedpoint(aid_hui->klob_a1, -27);
		iono->alpha_2 = float_to_fixedpoint(aid_hui->klob_a2, -24);
		iono->alpha_3 = float_to_fixedpoint(aid_hui->klob_a3, -24);
		iono->beta_0 = float_to_fixedpoint(aid_hui->klob_b0, 11);
		iono->beta_1 = float_to_fixedpoint(aid_hui->klob_b1, 14);
		iono->beta_2 = float_to_fixedpoint(aid_hui->klob_b2, 16);
		iono->beta_3 = float_to_fixedpoint(aid_hui->klob_b3, 16);
	}
}

static void
_ubx_msg_parse_aid_alm(struct ubx_hdr *hdr, void *pl, int pl_len, void *ud)
{
	struct ubx_aid_alm *aid_alm = pl;
	struct gps_assist_data *gps = ud;

	if(pl_len == 8) /* length if not available */
		return;
		
	if(pl_len != sizeof(struct ubx_aid_alm)) {
		fprintf(stderr, "pl_len != sizeof(struct ubx_aid_alm) (%d)\n", pl_len);
		return;
	}
	
	printd("[.] AID_ALM %2ld - %ld (nsv = %d)\n", aid_alm->sv_id, aid_alm->gps_week, gps->almanac.n_sv);

	if (aid_alm->gps_week) {
		int i = gps->almanac.n_sv++;
		gps->fields |= GPS_FIELD_ALMANAC;
		gps->almanac.wna = aid_alm->gps_week & 0xff;
		gps_unpack_sf45_almanac(aid_alm->alm_words, &gps->almanac.svs[i]);
		/* set satellite ID this way, otherwise it will be wrong */
		gps->almanac.svs[i].sv_id = aid_alm->sv_id;
	}
}

static void
_ubx_msg_parse_aid_eph(struct ubx_hdr *hdr, void *pl, int pl_len, void *ud)
{
	struct ubx_aid_eph *aid_eph = pl;
	struct gps_assist_data *gps = ud;

	if(pl_len == 8) /* length if not available */
		return;
	
	if(pl_len != sizeof(struct ubx_aid_eph)) {
		fprintf(stderr, "pl_len != sizeof(struct ubx_aid_eph) (%d)\n", pl_len);
		return;
	}
	
	printd("[.] AID_EPH %2ld - %s (nsv = %d)\n", aid_eph->sv_id, aid_eph->present ? "present" : "", gps->ephemeris.n_sv);

	if (aid_eph->present) {
		int i = gps->ephemeris.n_sv++;
		gps->fields |= GPS_FIELD_EPHEMERIS;
		gps->ephemeris.svs[i].sv_id = aid_eph->sv_id;
		gps_unpack_sf123(aid_eph->eph_words, &gps->ephemeris.svs[i]);
	}
}


static void
_ubx_msg_parse_nav_timegps(struct ubx_hdr *hdr, void *pl, int pl_len, void *ud)
{
	struct ubx_nav_timegps *nav_timegps = pl;
	struct gps_assist_data *gps = ud;

	printd1("[.] NAV_TIMEGPS\n");

	/* Extract info for "Reference Time" */
	gps->fields |= GPS_FIELD_REFTIME;

	gps->ref_time.wn = nav_timegps->week;
	gps->ref_time.tow = (double)nav_timegps->itow * 1e-3;
	gps->ref_time.when = time(NULL);

	printd1("  WN   %d\n", nav_timegps->week);
	printd1("  TOW  %ld\n", nav_timegps->itow);
}

/* Dispatch table */
struct ubx_dispatch_entry ubx_parse_dt[] = {
	UBX_DISPATCH(NAV, POSLLH, _ubx_msg_parse_nav_posllh),
	UBX_DISPATCH(AID, INI, _ubx_msg_parse_aid_ini),
	UBX_DISPATCH(AID, HUI, _ubx_msg_parse_aid_hui),
	UBX_DISPATCH(AID, ALM, _ubx_msg_parse_aid_alm),
	UBX_DISPATCH(AID, EPH, _ubx_msg_parse_aid_eph),
	UBX_DISPATCH(NAV, TIMEGPS, _ubx_msg_parse_nav_timegps),
};

