/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
*/
/*
 * Copyright 2018 Saso Kiselkov. All rights reserved.
 */

#include <string.h>

#include <XPLMDisplay.h>
#include <XPLMPlugin.h>
#include <XPLMProcessing.h>
#include <XPLMUtilities.h>

#include <acfutils/dr.h>
#include <acfutils/helpers.h>
#include <acfutils/math.h>
#include <acfutils/mt_cairo_render.h>
#include <acfutils/perf.h>

#include <opengpws/xplane_api.h>

#include "../api/openwxr/wxr_intf.h"
#include "../api/openwxr/xplane_api.h"

#include "fontmgr.h"
#include "standalone.h"

#define	MAX_SCREENS	4
#define	MAX_MODES	16
#define	MAX_COLORS	8

#define	EFIS_OFF_X	16
#define	EFIS_OFF_Y	15
#define	EFIS_WIDTH	194
#define	EFIS_HEIGHT	268
#define	MAX_DR_NAME	128

#define	NUM_DELAY_STEPS	10

#define	COLOR(c, scr)	((c) * ((pow(4.0, (scr)->brt / 0.75)) / 4.0))
#define	CYAN_RGB(scr)	COLOR(0, (scr)), COLOR(0.66, (scr)), COLOR(0.66, (scr))
#define	YELLOW_RGB(scr)	COLOR(1, (scr)), COLOR(0.8, (scr)), COLOR(0, (scr))
#define	RED_RGB(scr)	COLOR(1, (scr)), COLOR(0, (scr)), COLOR(0, (scr))
#define	GREEN_RGB(scr)	COLOR(0, (scr)), COLOR(1, (scr)), COLOR(0, (scr))
#define	BLACK_RGB(scr)	COLOR(0, (scr)), COLOR(0, (scr)), COLOR(0, (scr))
#define	PURPLE_RGB(scr)	COLOR(0.5, (scr)), COLOR(0, (scr)), COLOR(0.5, (scr))
#define	CYAN2_RGB(scr)	COLOR(0, (scr)), COLOR(0.7, (scr)), COLOR(0.9, (scr))
#define	GREEN2_RGB(scr)	COLOR(0.2, (scr)), COLOR(0.6, (scr)), COLOR(0.2, (scr))
#define	WHITE_RGB(scr)	COLOR(1, (scr)), COLOR(1, (scr)), COLOR(1, (scr))
#define	GRAY_RGB(scr)	COLOR(0.3, (scr)), COLOR(0.3, (scr)), COLOR(0.3, (scr))
#define	BLUE_RGB(scr)	COLOR(0, (scr)), COLOR(0, (scr)), COLOR(1, (scr))
#define	LIGHT_BLUE_RGB(scr)	COLOR(0.4, (scr)), COLOR(0.4, (scr)), COLOR(1, (scr))
static bool_t inited = B_FALSE;

typedef enum {
	RDR4B,
	RDR2000,
	WXR270,
	KONTUR,
	AW109SP
} ui_style_t;

typedef enum {
	TEXT_ALIGN_LEFT,
	TEXT_ALIGN_CENTER,
	TEXT_ALIGN_RIGHT
} text_align_t;

typedef struct {
	char		name[MAX_DR_NAME];
	bool_t		have_dr;
	dr_t		dr;
} delayed_dr_t;

typedef struct {
	double		value;
	double		delay;
	double		value_stack[NUM_DELAY_STEPS];
	double		stack_adv_t;
} delayed_ctl_t;

typedef enum {
	PANEL_RENDER_TYPE_2D = 0,
	PANEL_RENDER_TYPE_3D_UNLIT = 1,
	PANEL_RENDER_TYPE_3D_LIT = 2
} panel_render_type_t;

typedef struct wxr_sys_s wxr_sys_t;

typedef struct {
	double			x, y;
	double			w, h;
	double			scale;
	double			vert_mode_scale;
	double          	hrat;
	double			underscan;
	double          	voffset;
	double          	rvoffset;
	struct wxr_sys_s	*sys;
	mt_cairo_render_t	*mtcr;
	double			fps;

	double			power;
	double			power_on_rate;
	double			power_off_rate;
	double			brt;

	delayed_dr_t		power_dr;
	delayed_dr_t		power_sw_dr;
	delayed_ctl_t		power_sw_ctl;
	delayed_dr_t		brt_dr;
	double			scr_temp;
} wxr_scr_t;

typedef struct {
	char			name[16];
	vect2_t			stab_lim;
	unsigned		num_colors;
	wxr_color_t		colors[MAX_COLORS];
	wxr_color_t		base_colors[MAX_COLORS];
	bool_t is_wxr;
} mode_aux_info_t;

struct wxr_sys_s {
	double			power_on_time;
	double			power_on_delay;

	mutex_t			mode_lock;
	unsigned		num_modes;
	unsigned		cur_mode;
	wxr_conf_t		modes[MAX_MODES];
	mode_aux_info_t		aux[MAX_MODES];

	unsigned		num_screens;
	wxr_scr_t		screens[MAX_SCREENS];

	unsigned		efis_xywh[4];

	delayed_dr_t		power_dr;
	delayed_dr_t		power_sw_dr;
	delayed_dr_t		mode_dr;
	delayed_dr_t		tilt_dr;
	delayed_dr_t		range_dr;
	delayed_dr_t		gain_dr;
	delayed_dr_t		sepn_dr;
	delayed_dr_t		trk_dr;
	delayed_dr_t        	stab_dr;
	delayed_dr_t		nav_dr;
	delayed_dr_t        	vp_dr;


	delayed_ctl_t		power_sw_ctl;
	delayed_ctl_t		mode_ctl;
	delayed_ctl_t		range_ctl;
	delayed_ctl_t		tilt_ctl;

	double			range;
	double			gain_auto_pos;
	double			tilt;
	double			tilt_rate;

	double          	alert_rate;
	int             	trk;
	unsigned		nav;
	unsigned		vp;
	bool_t			shadow_enable;
	double          	sepn;
    	bool_t	    		trk_timer_flag;
    	bool_t          	alert_timer_flag;
    //unsigned    stab_mode;

	ui_style_t 		ui_style;

	bool_t			shared_egpws;
	const egpws_intf_t	*terr;
};

static wxr_sys_t sys = { 0 };
static XPLMWindowID debug_win = NULL;
static XPLMCommandRef open_debug_cmd = NULL;

static struct {
	dr_t		lat, lon, elev;
	dr_t		sim_time;
	dr_t		panel_render_type;
	dr_t		pitch, roll, hdg;
} drs;

static struct {
	dr_t	drefs_int[16];
	dr_t	drefs_float[16];
	int     dref_val_int[16];
	double   dref_val_float[16];
    	unsigned num_int;
    	unsigned num_float;
} cdrs;

static XPLMPluginID openwxr = XPLM_NO_PLUGIN_ID;
static void *atmo = NULL;
static void *wxr = NULL;
static openwxr_intf_t *wxr_intf = NULL;

static const egpws_conf_t egpws_conf = { .type = EGPWS_DB_ONLY };
static const egpws_range_t egpws_ranges[] = {
    { NM2MET(10), 100 },
    { NM2MET(25), 175 },
    { NM2MET(50), 250 },
    { NM2MET(100), 500 },
    { NM2MET(200), 1000 },
    { NAN, NAN }
};

#define	WXR_RES_X	320
#define	WXR_RES_Y	240

#define	MIN_GAIN	0.5	/* gain argument to WXR */
#define	MAX_GAIN	1.5	/* gain argument to WXR */
#define	DFL_GAIN	1.0	/* gain argument to WXR */

static void draw_debug_win(XPLMWindowID win, void *refcon);

#define	DELAYED_DR_OP(delayed_dr, op) \
	do { \
		delayed_dr_t *dd = (delayed_dr); \
		if (*dd->name != 0) { \
			if (!dd->have_dr) { \
				dd->have_dr = dr_find(&dd->dr, "%s", \
				    dd->name); \
			} \
			if (dd->have_dr) { \
				op; \
			} \
		} \
	} while (0)

static float
trk_timer_cb(float d_t, float elapsed, int counter, void *refcon)
{
	UNUSED(elapsed);
	UNUSED(counter);
	UNUSED(refcon);
	UNUSED(d_t);
	sys.trk_timer_flag = B_FALSE;
	return (0);
}

static float
alert_timer_cb(float d_t, float elapsed, int counter, void *refcon)
{
	UNUSED(elapsed);
	UNUSED(counter);
	UNUSED(refcon);
	UNUSED(d_t);
	if(wxr_intf->get_alert(wxr) == B_TRUE) 	wxr_intf->set_alert(wxr, B_FALSE);
	else 					wxr_intf->set_alert(wxr, B_TRUE);

	return (sys.alert_rate);
}


static void
delayed_ctl_set(delayed_ctl_t *ctl, double new_value)
{
	double now = dr_getf(&drs.sim_time);

	for (double delta = now - ctl->stack_adv_t; delta > ctl->delay / NUM_DELAY_STEPS; delta -= ctl->delay / NUM_DELAY_STEPS) 
	{
		ctl->stack_adv_t = now;
		ctl->value = ctl->value_stack[0];
		for (int i = 0; i < NUM_DELAY_STEPS - 1; i++)
			ctl->value_stack[i] = ctl->value_stack[i + 1];
		ctl->value_stack[NUM_DELAY_STEPS - 1] = new_value;
	}
}

static double delayed_ctl_get(delayed_ctl_t *ctl) UNUSED_ATTR;

static double
delayed_ctl_get(delayed_ctl_t *ctl)
{
	return (ctl->value);
}

static int
delayed_ctl_geti(delayed_ctl_t *ctl)
{
	return (round(ctl->value));
}

static int
open_debug_win(XPLMCommandRef ref, XPLMCommandPhase phase, void *refcon)
{
	UNUSED(ref);
	UNUSED(phase);
	UNUSED(refcon);

	if (debug_win == NULL) 
{
		XPLMCreateWindow_t cr = {
	    		.structSize 			= sizeof (cr), 
			.visible 			= B_TRUE,
		    	.left 				= 100, 
			.top 				= 400, 
			.right 				= 400, 
			.bottom 			= 100,
		    	.drawWindowFunc 		= draw_debug_win,
		    	.decorateAsFloatingWindow 	= xplm_WindowDecorationRoundRectangle,
		    	.layer 				= xplm_WindowLayerFloatingWindows
		};
		debug_win = XPLMCreateWindowEx(&cr);
	}
	XPLMSetWindowIsVisible(debug_win, B_TRUE);
	XPLMBringWindowToFront(debug_win);

	return (1);
}

static void
wxr_config(float d_t, const wxr_conf_t *mode, mode_aux_info_t *aux)
{
    	static unsigned last_mode = 0;
	unsigned range = 0;
	double tilt = 0, gain_ctl = 0.5;
	double gain = 0;
	double sepn = 0;
	double trk = 0;
    	unsigned stab = 1;
	unsigned vert = 0;

	bool_t power_on = B_TRUE, power_sw_on = B_TRUE, stby = B_FALSE;
	geo_pos3_t pos  =  GEO_POS3(dr_getf(&drs.lat), dr_getf(&drs.lon), dr_getf(&drs.elev));
	vect3_t orient  =  VECT3(dr_getf(&drs.pitch), dr_getf(&drs.hdg), dr_getf(&drs.roll));

	DELAYED_DR_OP(&sys.power_dr,  		power_on    = (dr_geti(&sys.power_dr.dr) != 0));
	DELAYED_DR_OP(&sys.power_sw_dr,	    	power_sw_on = (dr_geti(&sys.power_sw_dr.dr) != 0));
	delayed_ctl_set(&sys.power_sw_ctl, power_sw_on);
	power_sw_on = delayed_ctl_geti(&sys.power_sw_ctl);

	if (power_on && power_sw_on && mode->is_stby == B_FALSE) 
	{
		double now = dr_getf(&drs.sim_time);
		if (sys.power_on_time == 0)
			sys.power_on_time = now;
		stby = (now - sys.power_on_time < sys.power_on_delay);
	} 
	else 
	{
		stby = B_TRUE;
		sys.power_on_time = 0;
	}

	if(sys.cur_mode != last_mode)
    	{
        	wxr_intf->set_conf(wxr, mode);
        	last_mode = sys.cur_mode;
    	}

	wxr_intf->set_standby(wxr, stby);
	wxr_intf->set_stab(wxr, aux->stab_lim.x, aux->stab_lim.y);
	wxr_intf->set_acf_pos(wxr, pos, orient);

	if (sys.num_screens > 0)
		wxr_intf->set_brightness(wxr, sys.screens[0].brt / 0.75);

	wxr_intf->set_colors(wxr, aux->colors, aux->num_colors);

	DELAYED_DR_OP(&sys.range_dr, range = dr_geti(&sys.range_dr.dr));
	range 		= clampi(range, 0, mode->num_ranges - 1);
	delayed_ctl_set(&sys.range_ctl, range);
	range 		= delayed_ctl_geti(&sys.range_ctl);
	sys.range 	= mode->ranges[range];

	if (wxr_intf->get_scale(wxr) != range) 
	{
		bool_t new_scale = (mode->ranges[range] !=
		    mode->ranges[wxr_intf->get_scale(wxr)]);

		if (new_scale)
			wxr_intf->clear_screen(wxr);
		wxr_intf->set_scale(wxr, range);
		if (new_scale)
			wxr_intf->clear_screen(wxr);
	}

	if(mode->is_alert == B_TRUE && sys.alert_timer_flag == B_FALSE)    
	{
        	XPLMSetFlightLoopCallbackInterval(alert_timer_cb, sys.alert_rate, 1, NULL);
        	sys.alert_timer_flag = B_TRUE;
        	wxr_intf->set_alert(wxr, B_TRUE);
    	}	
    	else if(mode->is_alert == B_FALSE && sys.alert_timer_flag == B_TRUE)
	{
        	XPLMSetFlightLoopCallbackInterval(alert_timer_cb, 0, 1, NULL);
	        sys.alert_timer_flag = B_FALSE;
        	wxr_intf->set_alert(wxr, B_FALSE);
	}

	DELAYED_DR_OP(&sys.tilt_dr, 		tilt = dr_getf(&sys.tilt_dr.dr));
	delayed_ctl_set(&sys.tilt_ctl, tilt);
	tilt = delayed_ctl_get(&sys.tilt_ctl);
	FILTER_IN_LIN(sys.tilt, tilt, d_t, sys.tilt_rate);
	wxr_intf->set_ant_pitch(wxr, sys.tilt);

	DELAYED_DR_OP(&sys.gain_dr,   		gain_ctl = dr_getf(&sys.gain_dr.dr));
	if (fabs(gain_ctl - sys.gain_auto_pos) < 1e-5)
		gain = DFL_GAIN;
	else
		gain = wavg(MIN_GAIN, MAX_GAIN, clamp(gain_ctl, 0, 1));
	wxr_intf->set_gain(wxr, gain);

	DELAYED_DR_OP(&sys.sepn_dr,		sepn = dr_getf(&sys.sepn_dr.dr));
	sys.sepn = clamp(sepn, 0, 1);
	sys.aux[2].colors[1].min_val = 0.8*(1 - sys.sepn/2);

	DELAYED_DR_OP(&sys.trk_dr,		trk = dr_geti(&sys.trk_dr.dr));
        if(sys.trk != trk)
	{
        	sys.trk_timer_flag = B_TRUE;
        	XPLMSetFlightLoopCallbackInterval(trk_timer_cb, 15, 1, NULL);
		sys.trk = clamp(trk, -90, 90);
		if(wxr_intf->get_vert_mode(wxr) == B_TRUE)
		{
			wxr_intf->set_vert_mode(wxr, B_TRUE, (float)sys.trk);
		}
        }


    	DELAYED_DR_OP(&sys.stab_dr,            stab = dr_geti(&sys.stab_dr.dr));
        if(stab)
        {
        	wxr_intf->set_stab(wxr, aux->stab_lim.x, aux->stab_lim.x);
        }
        else
        {
        	wxr_intf->set_stab(wxr, 0, 0);
        }
	DELAYED_DR_OP(&sys.nav_dr,            	sys.nav = dr_geti(&sys.nav_dr.dr));
	DELAYED_DR_OP(&sys.vp_dr,            	vert = dr_geti(&sys.vp_dr.dr));
	if (vert != sys.vp)
	{
		sys.vp = vert;
		if (vert == 1)
		{
			wxr_intf->set_vert_mode(wxr, B_TRUE, (float)sys.trk);
		}
		else
		{
			wxr_intf->set_vert_mode(wxr, B_FALSE, 0.0);
			
		}
	}

	if(wxr_intf->get_beam_shadow(wxr) != sys.shadow_enable) 
		wxr_intf->set_beam_shadow(wxr, sys.shadow_enable);
}


static float
floop_cb(float d_t, float elapsed, int counter, void *refcon)
{
	const wxr_conf_t *mode;
	mode_aux_info_t *aux;
	unsigned new_mode = 0;

	UNUSED(elapsed);
	UNUSED(counter);
	UNUSED(refcon);

	if (sys.shared_egpws && !sys.terr->is_inited())
		return (-1);

	/*
	 * Set up OpenGPWS as we need it:
	 * 1) DB-ONLY mode (no active EGPWS callouts)
	 * 2) enable sound playback (for our PWS callouts)
	 * 3) position known to run the terrain DB
	 * 4) nav systems on
	 */
	if (!sys.shared_egpws) 
	{
		sys.terr->set_state(&egpws_conf);
		sys.terr->set_sound_inh(B_FALSE);
		sys.terr->set_ranges(egpws_ranges);
		sys.terr->set_pos_ok(B_TRUE);
		sys.terr->set_nav_on(B_TRUE, B_TRUE);
	}

	mutex_enter(&sys.mode_lock);
	DELAYED_DR_OP(&sys.mode_dr, 	new_mode = dr_geti(&sys.mode_dr.dr));
	new_mode = MIN(new_mode, sys.num_modes - 1);
	delayed_ctl_set(&sys.mode_ctl, new_mode);
	sys.cur_mode = delayed_ctl_geti(&sys.mode_ctl);

	mutex_exit(&sys.mode_lock);

	mode = &sys.modes[sys.cur_mode];
	aux = &sys.aux[sys.cur_mode];
	if (wxr != NULL && mode->num_ranges == 0) 
	{
		wxr_intf->fini(wxr);
		wxr = NULL;
	}

	if (wxr == NULL && mode->num_ranges != 0) 
	{
		wxr = wxr_intf->init(mode, atmo);
		ASSERT(wxr != NULL);
	}

	if (wxr != NULL)
		wxr_config(d_t, mode, aux);

	for (unsigned i = 0; wxr != NULL && i < sys.num_screens; i++) 
	{
		bool_t power = B_TRUE, sw = B_TRUE;
		wxr_scr_t *scr = &sys.screens[i];
		double brt = 0.75;

		DELAYED_DR_OP(&scr->power_dr,		    power = (dr_geti(&scr->power_dr.dr) != 0));
		DELAYED_DR_OP(&scr->power_sw_dr,	    sw = (dr_geti(&scr->power_sw_dr.dr) != 0));
		delayed_ctl_set(&scr->power_sw_ctl, sw);
		sw = delayed_ctl_geti(&scr->power_sw_ctl);

		if (power && sw) 
		{
			double rate = (scr->power_on_rate / (1 + 50 * POW3(scr->scr_temp)));
			FILTER_IN_LIN(scr->scr_temp, 1, d_t, 10);
			FILTER_IN_LIN(scr->power, 1, d_t, rate);
		} 
		else 
		{
			FILTER_IN_LIN(scr->scr_temp, 0, d_t, 600);
			FILTER_IN_LIN(scr->power, 0, d_t, scr->power_off_rate);
		}

		if (scr->power < 0.01 || scr->power > 0.99)
			mt_cairo_render_set_fps(scr->mtcr, scr->fps);
		else
			mt_cairo_render_set_fps(scr->mtcr, 20);

		DELAYED_DR_OP(&scr->brt_dr, brt = dr_getf(&scr->brt_dr.dr));

		if (brt > scr->brt)
			FILTER_IN_LIN(scr->brt, brt, d_t, 1);
		else
			FILTER_IN_LIN(scr->brt, brt, d_t, 0.2);
	}
	return (-1);
}

static int
draw_cb(XPLMDrawingPhase phase, int before, void *refcon)
{
	egpws_render_t render = { .do_draw = B_FALSE };

	UNUSED(phase);
	UNUSED(before);
	UNUSED(refcon);

//	if (dr_geti(&drs.panel_render_type) != PANEL_RENDER_TYPE_3D_LIT)
//		return (1);

	/*
	 * Even though we don't draw tiles, we need to let OpenGPWS when
	 * to perform tile setup.
	 */
	sys.terr->terr_render(&render);

	for (unsigned i = 0; wxr != NULL && i < sys.num_screens; i++) 
	{
		wxr_scr_t *scr = &sys.screens[i];
		if( wxr_intf->get_vert_mode(wxr) == B_TRUE)
		{
			double center_y = scr->y + scr->h / 2;
			double sz = scr->w * scr->scale * scr->vert_mode_scale;
			wxr_intf->draw(wxr, VECT2(scr->x, center_y - sz), VECT2 (sz, sz * 2) );
		}
		else
		{
			double center_x = scr->x + scr->w / 2;
			double sz = scr->h * scr->underscan * scr->scale;
			wxr_intf->draw(wxr, VECT2(center_x - sz, scr->y + scr->voffset + scr->rvoffset), VECT2(2 * sz, sz * scr->hrat));
		}
		mt_cairo_render_draw(scr->mtcr, VECT2(scr->x, scr->y + scr->voffset), VECT2(scr->w, scr->h));
	}

	return (1);
}

static void
draw_debug_win(XPLMWindowID win, void *refcon)
{
	int left, top, right, bottom, width, height;
	unsigned scr_id = (intptr_t)refcon;
	wxr_scr_t *scr;

	ASSERT3U(scr_id, <, sys.num_screens);
	scr = &sys.screens[scr_id];

	XPLMGetWindowGeometry(win, &left, &top, &right, &bottom);
	width = right - left;
	height = top - bottom;

	if (wxr != NULL)
		wxr_intf->draw(wxr, VECT2(left, bottom), VECT2(width, height));

	mt_cairo_render_draw(scr->mtcr, VECT2(left, bottom), VECT2(width, height));
}

static void
align_text(cairo_t *cr, const char *buf, double x, double y, text_align_t how)
{
	cairo_text_extents_t te;

	cairo_text_extents(cr, buf, &te);
	switch (how) {
	case TEXT_ALIGN_LEFT:
		cairo_move_to(cr, x - te.x_bearing, y - te.height / 2 - te.y_bearing);
		break;
	case TEXT_ALIGN_CENTER:
		cairo_move_to(cr, x - te.width / 2 - te.x_bearing, y - te.height / 2 - te.y_bearing);
		break;
	case TEXT_ALIGN_RIGHT:
		cairo_move_to(cr, x - te.width - te.x_bearing, y - te.height / 2 - te.y_bearing);
		break;
	}
}

static void
render_ui(cairo_t *cr, wxr_scr_t *scr)
{
	enum { FONT_SZ = 20, LINE_HEIGHT = 20, TOP_OFFSET = -FONT_SZ / 5 };
	char buf[16];
	double dashes[] = { 5, 5 };
	char mode_name[16];
	int offset = -10;
	
	if (wxr != NULL) 
	{
	    	switch(sys.ui_style){
	   	case RDR4B:
			cairo_set_source_rgb(cr, CYAN_RGB(scr));
			cairo_set_line_width(cr, 1);

			if(sys.cur_mode == 1)
			{
			    cairo_save(cr);
			    cairo_move_to(cr, 0, 0);
			    double r = (WXR_RES_Y /4)/2;
			    cairo_set_source_rgb(cr, GREEN_RGB(scr));
			    cairo_set_line_width(cr, WXR_RES_Y / 4);

			    cairo_arc(cr, 0, 0, r, DEG2RAD(180), DEG2RAD(360));
			    cairo_stroke(cr);

			    cairo_set_source_rgb(cr, YELLOW_RGB(scr));
			    cairo_arc(cr, 0, 0, r*3, DEG2RAD(180), DEG2RAD(360));
			    cairo_stroke(cr);

			    cairo_set_source_rgb(cr, RED_RGB(scr));
			    cairo_arc(cr, 0, 0, r*5, DEG2RAD(180), DEG2RAD(360));
			    cairo_stroke(cr);

			    cairo_set_source_rgb(cr, PURPLE_RGB(scr));
			    cairo_arc(cr, 0, 0, r*7, DEG2RAD(180), DEG2RAD(360));
			    cairo_stroke(cr);

			    cairo_restore(cr);
			}

			for (int angle = -90; angle <= 90; angle += 30) 
			{
			    cairo_save(cr);
			    cairo_rotate(cr, DEG2RAD(angle));
			    cairo_move_to(cr, 0, 0);
			    cairo_rel_line_to(cr, 0, -WXR_RES_Y);
			    cairo_stroke(cr);
			    cairo_restore(cr);
			}

			cairo_set_dash(cr, dashes, 2, 0);

			for (int i = 0; i < 4; i++) 
			{
			    cairo_arc(cr, 0, 0, (WXR_RES_Y / 4) * (i + 1), DEG2RAD(180), DEG2RAD(360));
			    cairo_stroke(cr);
			}
			cairo_set_dash(cr, NULL, 0, 0);

			cairo_set_font_face(cr, fontmgr_get(FONTMGR_EFIS_FONT));
			cairo_set_font_size(cr, FONT_SZ);

			snprintf(buf, sizeof (buf), "RNG %3.0f", MET2NM(sys.range));
			align_text(cr, buf, -WXR_RES_X / 2, -WXR_RES_Y + TOP_OFFSET,
			    TEXT_ALIGN_LEFT);
			cairo_show_text(cr, buf);

			mutex_enter(&sys.mode_lock);
			strlcpy(mode_name, sys.aux[sys.cur_mode].name, sizeof (mode_name));
			mutex_exit(&sys.mode_lock);

			align_text(cr, mode_name, -WXR_RES_X / 2, -WXR_RES_Y + TOP_OFFSET + LINE_HEIGHT, TEXT_ALIGN_LEFT);
			cairo_show_text(cr, mode_name);

			snprintf(buf, sizeof (buf), "MRK %.3g", MET2NM(sys.range / 4));
			align_text(cr, buf, WXR_RES_X / 2, -WXR_RES_Y + TOP_OFFSET, TEXT_ALIGN_RIGHT);
			cairo_show_text(cr, buf);

			if (sys.tilt >= 0.05)
			    snprintf(buf, sizeof (buf), "%.1f\u2191", sys.tilt);
			else if (sys.tilt <= -0.05)
			    snprintf(buf, sizeof (buf), "%.1f\u2193", ABS(sys.tilt));
			else
			    snprintf(buf, sizeof (buf), "0.0\u00a0");

			align_text(cr, buf, WXR_RES_X / 2, -WXR_RES_Y + TOP_OFFSET + LINE_HEIGHT, TEXT_ALIGN_RIGHT);
			cairo_show_text(cr, buf);

			break;

		case RDR2000:
			cairo_set_font_face(cr, fontmgr_get(FONTMGR_EFIS_FONT));
			cairo_set_font_size(cr, FONT_SZ);

			if(wxr_intf->get_vert_mode(wxr) == B_FALSE)
			{

				if(sys.cur_mode == 1)
				{
					cairo_save(cr);
						cairo_move_to(cr, 0, 0);
						double r = (WXR_RES_Y /4);
						cairo_set_source_rgb(cr, GREEN_RGB(scr));
						cairo_set_line_width(cr, WXR_RES_Y / 8);
						cairo_stroke(cr);

						cairo_arc(cr, 0, 0, r, DEG2RAD(220), DEG2RAD(320));
						cairo_stroke(cr);

						cairo_set_source_rgb(cr, YELLOW_RGB(scr));
						cairo_arc(cr, 0, 0, r*1.5, DEG2RAD(220), DEG2RAD(320));
						cairo_stroke(cr);

						cairo_set_source_rgb(cr, RED_RGB(scr));
						cairo_arc(cr, 0, 0, r*2, DEG2RAD(220), DEG2RAD(320));
						cairo_stroke(cr);

						cairo_set_source_rgb(cr, PURPLE_RGB(scr));
						cairo_arc(cr, 0, 0, r*2.5, DEG2RAD(220), DEG2RAD(320));
						cairo_stroke(cr);
					cairo_restore(cr);
				}

				if(sys.trk_timer_flag == B_TRUE)
				{
					cairo_set_source_rgb(cr, YELLOW_RGB(scr));
					dashes[0] = 3;
					dashes[1] = 3;
					cairo_set_line_width(cr, 2);

					cairo_save(cr);
						cairo_move_to(cr, 0, 0);
						cairo_rotate(cr, DEG2RAD(sys.trk));
						cairo_rel_line_to(cr, 0, -WXR_RES_Y);
						cairo_stroke(cr);
					cairo_restore(cr);

					snprintf(buf, sizeof (buf), "%2u\u00B0", ABS(sys.trk));
					align_text(cr, buf, -WXR_RES_X / 2 + FONT_SZ*2, -WXR_RES_Y - TOP_OFFSET*2 + LINE_HEIGHT, TEXT_ALIGN_RIGHT);
					cairo_show_text(cr, buf);
				}

				cairo_set_source_rgb(cr, CYAN_RGB(scr));
				dashes[0] = 2;
				dashes[1] = 7;
				cairo_set_dash(cr, dashes, 2, 0);
				cairo_move_to(cr, 0, 0);
				cairo_rel_line_to(cr, 0, -WXR_RES_Y);
				cairo_stroke(cr);
				cairo_set_dash(cr, NULL, 0, 0);

				dashes[0] = 2;
				dashes[1] = 10;
				cairo_set_dash(cr, dashes, 2, 0);

				for (int i = 0; i < 4; i++) 
				{
					cairo_arc(cr, 0, 0, (WXR_RES_Y / 4) * (i + 1), DEG2RAD(220), DEG2RAD(270));
					cairo_stroke(cr);
				}

				for (int i = 0; i < 4; i++) 
				{
					cairo_arc_negative(cr, 0, 0, (WXR_RES_Y / 4) * (i + 1), DEG2RAD(320), DEG2RAD(270));
					cairo_stroke(cr);
				}

				cairo_set_dash(cr, NULL, 0, 0);

				snprintf(buf, sizeof (buf), "%3.0f", MET2NM(sys.range) );
				align_text(cr, buf, WXR_RES_X / 2, -WXR_RES_Y - TOP_OFFSET*2 + 27, TEXT_ALIGN_RIGHT);
				cairo_show_text(cr, buf);

				snprintf(buf, sizeof (buf), "%.3g", MET2NM(sys.range - sys.range/4) );
				align_text(cr, buf, WXR_RES_X / 2.45, -((WXR_RES_Y)/ 4) * 2 + 25, TEXT_ALIGN_LEFT);
				cairo_show_text(cr, buf);

				snprintf(buf, sizeof (buf), "%2.0f", MET2NM(sys.range - (sys.range/4)*2 ) );
				align_text(cr, buf, WXR_RES_X / 3.2, -(WXR_RES_Y/ 4), TEXT_ALIGN_LEFT);
				cairo_show_text(cr, buf);

				snprintf(buf, sizeof (buf), "%.3g", MET2NM(sys.range - (sys.range/4)*3 ) );
				align_text(cr, buf, WXR_RES_X / 6.8, -(WXR_RES_Y/ 4) / 2 + 10, TEXT_ALIGN_LEFT);
				cairo_show_text(cr, buf);

				mutex_enter(&sys.mode_lock);
				strlcpy(mode_name, sys.aux[sys.cur_mode].name, sizeof (mode_name));
				mutex_exit(&sys.mode_lock);

				align_text(cr, mode_name, -WXR_RES_X / 2, -60, TEXT_ALIGN_LEFT);
				cairo_show_text(cr, mode_name);

				if (sys.nav == 1)
				{
					cairo_set_font_size(cr, FONT_SZ - 5);

					snprintf(buf, sizeof (buf), "NO NAV");
					align_text(cr, buf, -WXR_RES_X / 2, -60 + FONT_SZ, TEXT_ALIGN_LEFT);
					cairo_show_text(cr, buf);
					cairo_set_font_size(cr, FONT_SZ);
				}

				vect3_t pos;
				vect2_t lim;

				if      (sys.tilt >= 0.05)	snprintf(buf, sizeof (buf), "UP %.1f\u00B0", sys.tilt);
				else if (sys.tilt <= -0.05)	snprintf(buf, sizeof (buf), "DN %.1f\u00B0", ABS(sys.tilt));
				else				snprintf(buf, sizeof (buf), "0.0\u00B0");

				align_text(cr, buf, WXR_RES_X / 2, -WXR_RES_Y - TOP_OFFSET*2 , TEXT_ALIGN_RIGHT);
				cairo_show_text(cr, buf);

				snprintf(buf, sizeof (buf), "%1.1f", wxr_intf->get_gain(wxr));
				align_text(cr, buf, -WXR_RES_X / 2, -20, TEXT_ALIGN_LEFT);
				cairo_show_text(cr, buf);


				wxr_intf->get_acf_pos(wxr, &NULL_GEO_POS3, &pos);
				wxr_intf->get_stab(wxr, &lim.x, &lim.y);

				if(lim.x == 0 || lim.y == 0)
				{
					snprintf(buf, sizeof (buf), "STAB OFF");// (1 == 0) ?("ON") :("OFF")
					align_text(cr, buf, -WXR_RES_X / 2, -WXR_RES_Y - TOP_OFFSET*2, TEXT_ALIGN_LEFT);
					cairo_show_text(cr, buf);
				}
				else if(ABS(pos.x) > lim.x || ABS(pos.z) > lim.y)
				{
					snprintf(buf, sizeof (buf), "STAB LMT");
					align_text(cr, buf, WXR_RES_X / 2, -20,	TEXT_ALIGN_RIGHT);
					cairo_show_text(cr, buf);
				}
			}
			else
			{
				cairo_set_source_rgb(cr, CYAN_RGB(scr));
				dashes[0] = 2;
				dashes[1] = 7;
				cairo_set_dash(cr, dashes, 2, 0);

				for(unsigned i = 1; i < 4; i++)
				{
					cairo_move_to(cr, (-WXR_RES_X/2) + (WXR_RES_X * scr->vert_mode_scale / 4)  , -WXR_RES_Y / 4 * i);
					cairo_rel_line_to(cr, WXR_RES_X * scr->vert_mode_scale * 0.75, 0);
					cairo_stroke(cr);
				}

				int alt = roundmul(MET2FEET(sys.range/4.05)/1000, 5);

				snprintf(buf, sizeof (buf), "+%d", alt);
				align_text(cr, buf, (WXR_RES_X/3)+FONT_SZ/2, -WXR_RES_Y/4*3, TEXT_ALIGN_LEFT);
				cairo_show_text(cr, buf);
				cairo_stroke(cr);

				snprintf(buf, sizeof (buf), "0");
				align_text(cr, buf, (WXR_RES_X/3)+FONT_SZ, -WXR_RES_Y/2, TEXT_ALIGN_LEFT);
				cairo_show_text(cr, buf);
				cairo_stroke(cr);

				snprintf(buf, sizeof (buf), "-%d", alt);
				align_text(cr, buf, (WXR_RES_X/3)+FONT_SZ/2, -WXR_RES_Y/4, TEXT_ALIGN_LEFT);
				cairo_show_text(cr, buf);
				cairo_stroke(cr);


				for (int i = 0; i < 4; i++) 
				{
					double x = 0;
					double y = 0;

					cairo_arc(cr, -WXR_RES_X/2, -WXR_RES_Y/2, (WXR_RES_X / 4) * (i + 1) * scr->vert_mode_scale, DEG2RAD(-30), DEG2RAD(30));
					cairo_get_current_point(cr, &x, &y);
					cairo_stroke(cr);

					if(i < 3)
					{
						snprintf(buf, sizeof (buf), "%.3g", MET2NM((sys.range/4)*(i+1) ));
						align_text(cr, buf, x , y + FONT_SZ/2 - 5 , TEXT_ALIGN_RIGHT);
						cairo_show_text(cr, buf);
					}
					else
					{
						snprintf(buf, sizeof (buf), "%.3g", MET2NM((sys.range/4)*(i+1) ));
						align_text(cr, buf, x + FONT_SZ + 2, y - FONT_SZ*2 + FONT_SZ/2 , TEXT_ALIGN_LEFT);
						cairo_show_text(cr, buf);
					}

					cairo_stroke(cr);
				}

				cairo_set_dash(cr, NULL, 0, 0);

				vect3_t pos;
				vect2_t lim;

				mutex_enter(&sys.mode_lock);
				strlcpy(mode_name, sys.aux[sys.cur_mode].name, sizeof (mode_name));
				mutex_exit(&sys.mode_lock);

				align_text(cr, mode_name, -WXR_RES_X / 2, -60, TEXT_ALIGN_LEFT);
				cairo_show_text(cr, mode_name);

				snprintf(buf, sizeof (buf), "%1.1f", wxr_intf->get_gain(wxr) - 0.5);
				align_text(cr, buf, -WXR_RES_X / 2, -20, TEXT_ALIGN_LEFT);
				cairo_show_text(cr, buf);

				wxr_intf->get_acf_pos(wxr, &NULL_GEO_POS3, &pos);
				wxr_intf->get_stab(wxr, &lim.x, &lim.y);

				snprintf(buf, sizeof (buf), "PROFILE");
//				align_text(cr, buf, -WXR_RES_X / 2, -WXR_RES_Y - TOP_OFFSET * 2, TEXT_ALIGN_LEFT);
				align_text(cr, buf, -WXR_RES_X / 4, -WXR_RES_Y - TOP_OFFSET * 2, TEXT_ALIGN_LEFT);
				cairo_show_text(cr, buf);

				snprintf(buf, sizeof (buf), "R %+2d", sys.trk);
//				align_text(cr, buf, -WXR_RES_X / 2, -WXR_RES_Y - TOP_OFFSET * 2 + FONT_SZ, TEXT_ALIGN_LEFT);
				align_text(cr, buf, -WXR_RES_X / 4, -WXR_RES_Y - TOP_OFFSET * 2 + FONT_SZ, TEXT_ALIGN_LEFT);
				cairo_show_text(cr, buf);

				if((ABS(pos.x) > lim.x || ABS(pos.z) > lim.y) && lim.x > 0 && lim.y > 0)
				{
					snprintf(buf, sizeof (buf), "STAB LMT");
					align_text(cr, buf, WXR_RES_X / 2, -40, TEXT_ALIGN_RIGHT);
					cairo_show_text(cr, buf);
				}
			}
			break;

		case WXR270:

			cairo_set_font_face(cr, fontmgr_get(FONTMGR_EFIS_FONT));
			cairo_set_font_size(cr, FONT_SZ);
			cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND);
			cairo_set_line_width(cr, 2);

			if(sys.cur_mode == 2)
			{

			    cairo_save(cr);
			    cairo_set_line_cap (cr, CAIRO_LINE_CAP_BUTT);
			    double r =((WXR_RES_Y + offset) / 5)/2;
			    cairo_set_source_rgb(cr, GREEN_RGB(scr));
			    cairo_set_line_width(cr, ((WXR_RES_Y + offset) / 5)/2);
			    cairo_arc(cr, 0, offset, r, DEG2RAD(210), DEG2RAD(330));
			    cairo_stroke(cr);

			    cairo_set_source_rgb(cr, YELLOW_RGB(scr));
			    cairo_set_line_width(cr, ((WXR_RES_Y + offset) / 5));
			    cairo_arc(cr, 0, offset, r*2, DEG2RAD(210), DEG2RAD(330));
			    cairo_stroke(cr);

			    cairo_set_source_rgb(cr, RED_RGB(scr));
			    cairo_arc(cr, 0, offset, r*4, DEG2RAD(210), DEG2RAD(330));
			    cairo_stroke(cr);

			    cairo_set_source_rgb(cr, YELLOW_RGB(scr));
			    cairo_arc(cr, 0, offset, r*6, DEG2RAD(210), DEG2RAD(330));
			    cairo_stroke(cr);

			    cairo_set_source_rgb(cr, GREEN_RGB(scr));
			    cairo_set_line_width(cr, ((WXR_RES_Y + offset) / 5)/2);
			    cairo_arc(cr, 0, offset, r*7 + r/2, DEG2RAD(210), DEG2RAD(330));
			    cairo_stroke(cr);
			    cairo_restore(cr);
			}

			if(sys.cur_mode == 4) cairo_set_source_rgb(cr, GREEN2_RGB(scr) );
			else cairo_set_source_rgb(cr, CYAN2_RGB(scr) );

			cairo_save(cr);
			cairo_set_line_width(cr, 3);
			cairo_move_to(cr, 0, -2);
			cairo_rel_line_to(cr, 0, -8);
			cairo_move_to(cr, 0, -8);
			cairo_rel_line_to(cr, -5, 5);
			cairo_move_to(cr, 0, -8);
			cairo_rel_line_to(cr, 5, 5);
			cairo_move_to(cr, 0, -2);
			cairo_rel_line_to(cr, -2, 2);
			cairo_move_to(cr, 0, -2);
			cairo_rel_line_to(cr, 2, 2);
			cairo_stroke (cr);
			cairo_restore(cr);

		      	for (int i = 0; i < 5; i++) 
			{
		    		double r = ((WXR_RES_Y + offset) / 5) * (i + 1);

		    		if( i == 0)
				{
				cairo_arc(cr, 0, offset, r, DEG2RAD(210), DEG2RAD(330));
				cairo_stroke(cr);
				for (int angle = 150; angle <= 210; angle += 30) 
				{
					double ang = DEG2RAD(angle);
					double x = r*sin(ang);
					double y = r*cos(ang);
					double x2 = (r+5)*sin(ang);
					double y2 = (r+5)*cos(ang);
					cairo_move_to(cr, x, y + offset);
					cairo_line_to(cr, x2, y2 + offset);
					cairo_stroke(cr);
				}

				double ang = DEG2RAD(120);
				double x = r*sin(ang);
				double y = r*cos(ang);
				snprintf(buf, sizeof (buf), "%3.0f", MET2NM(sys.range)/5);
				align_text(cr, buf, x + 5, y + FONT_SZ/2 + offset, TEXT_ALIGN_LEFT);
				cairo_show_text(cr, buf);
		    	}
		    	else if(i == 3)
			{
				cairo_arc(cr, 0, offset, r, DEG2RAD(210), DEG2RAD(310));
				cairo_stroke(cr);
				cairo_arc(cr, 0, offset, r, DEG2RAD(319), DEG2RAD(330));
				cairo_stroke(cr);

				for (int angle = 150; angle <= 210; angle += 30) 
				{
					double ang = DEG2RAD(angle);
					double x = (r-5)*sin(ang);
					double y = (r-5)*cos(ang);
					double x2 = (r+5)*sin(ang);
					double y2 = (r+5)*cos(ang);
					cairo_move_to(cr, x, y + offset);
					cairo_line_to(cr, x2, y2 + offset);
					cairo_stroke(cr);
				}

				double ang = DEG2RAD(140);
				double x = r*sin(ang);
				double y = r*cos(ang);

				snprintf(buf, sizeof (buf), "%3.0f", MET2NM(sys.range)/5*(i+1));
				align_text(cr, buf, x + 10 , y + FONT_SZ/2 + offset, TEXT_ALIGN_CENTER);
				cairo_show_text(cr, buf);
		    	}
			else if(i == 4)
			{
				cairo_arc(cr, 0, offset, r, DEG2RAD(210), DEG2RAD(330));
				cairo_stroke(cr);
				for (int angle = 150; angle <= 210; angle += 30) 
				{
					double ang = DEG2RAD(angle);
					double x = r*sin(ang);
					double y = r*cos(ang);
					double x2 = (r-5)*sin(ang);
					double y2 = (r-5)*cos(ang);
					cairo_move_to(cr, x , y + offset);
					cairo_line_to(cr, x2, y2 + offset);
					cairo_stroke(cr);
				}

				double ang = DEG2RAD(150);
				double x = (r+FONT_SZ*2)*sin(ang);
				double y = (r+FONT_SZ*2)*cos(ang);

				snprintf(buf, sizeof (buf), "%3.0f", MET2NM(sys.range));
				align_text(cr, buf, x - 10, y + 10 + FONT_SZ + offset, TEXT_ALIGN_LEFT);
				cairo_show_text(cr, buf);

		    	}
		    	else
			{
				cairo_arc(cr, 0, offset, r, DEG2RAD(210), DEG2RAD(330));
				cairo_stroke(cr);

				for (int angle = 150; angle <= 210; angle += 30) 
				{
					double ang = DEG2RAD(angle);
					double x = (r-5)*sin(ang);
					double y = (r-5)*cos(ang);
					double x2 = (r+5)*sin(ang);
					double y2 = (r+5)*cos(ang);
					cairo_move_to(cr, x, y + offset);
					cairo_line_to(cr, x2, y2 + offset);
					cairo_stroke(cr);
				}

				double ang = DEG2RAD(120);
				double x = r*sin(ang);
				double y = r*cos(ang);

				snprintf(buf, sizeof (buf), "%3.0f", MET2NM(sys.range)/5*(i+1));
				align_text(cr, buf, x + 5, y + FONT_SZ/2 + offset, TEXT_ALIGN_LEFT);
				cairo_show_text(cr, buf);

				if (i==2)
				{
					ang = DEG2RAD(245);
					x = r*sin(ang) + offset;
					y = r*cos(ang) + offset;
					mutex_enter(&sys.mode_lock);

					if(wxr != NULL && wxr_intf->get_gain(wxr) != DFL_GAIN && sys.modes[sys.cur_mode].is_stby == B_FALSE)
					{
						strlcpy(mode_name, "GAIN", sizeof (mode_name));
					}
					else{
						strlcpy(mode_name, sys.aux[sys.cur_mode].name, sizeof (mode_name));
					}

					mutex_exit(&sys.mode_lock);
					align_text(cr, mode_name, x , y + FONT_SZ/2 + offset, TEXT_ALIGN_CENTER);
					cairo_show_text(cr, mode_name);
				}
			    }
			    cairo_stroke(cr);
			}

			if(wxr != NULL && wxr_intf->get_gain(wxr) == DFL_GAIN && sys.modes[sys.cur_mode].is_stby == B_FALSE)
			{
				snprintf(buf, sizeof (buf), "T");
				cairo_save(cr);
				cairo_text_extents_t te;
				cairo_text_extents(cr, buf, &te);
				cairo_set_source_rgb(cr, RED_RGB(scr));
				cairo_set_line_width(cr, 1);
				cairo_rectangle(cr,  WXR_RES_X/3 - 1 , -WXR_RES_Y, te.x_advance + 2, te.height + 2);
				cairo_stroke_preserve(cr);
				cairo_fill(cr);


				cairo_set_source_rgb(cr, YELLOW_RGB(scr));
				align_text(cr, buf, WXR_RES_X/3 , -WXR_RES_Y - TOP_OFFSET*2, TEXT_ALIGN_LEFT);
				cairo_show_text(cr, buf);
				cairo_stroke(cr);
				cairo_restore(cr);
			}

			cairo_save(cr);
			if  (sys.cur_mode == 4) cairo_set_source_rgb(cr, GREEN2_RGB(scr) );
			else 			cairo_set_source_rgb(cr, CYAN2_RGB(scr) );

			if (sys.tilt >= 0.05)
			    snprintf(buf, sizeof (buf), "%.1f\u2191", sys.tilt);
			else if (sys.tilt <= -0.05)
			    snprintf(buf, sizeof (buf), "%.1f\u2193", ABS(sys.tilt));
			else
			    snprintf(buf, sizeof (buf), "0.0\u00a0");

			align_text(cr, buf, WXR_RES_X / 2, -FONT_SZ/2, TEXT_ALIGN_RIGHT);
			cairo_show_text(cr, buf);
			snprintf(buf, sizeof (buf), "%1.1f", wxr_intf->get_gain(wxr) - MIN_GAIN);
			align_text(cr, buf, -WXR_RES_X / 2, -FONT_SZ/2, TEXT_ALIGN_LEFT);
			cairo_show_text(cr, buf);
			cairo_stroke(cr);
			cairo_restore(cr);

	       		break;
		case KONTUR:
			cairo_set_font_face(cr, fontmgr_get(FONTMGR_EFIS_FONT));
			cairo_set_font_size(cr, FONT_SZ);
			cairo_text_extents_t tx;

			// draw own plane
			cairo_set_source_rgb(cr, CYAN_RGB(scr));
			cairo_set_line_width(cr, 4);
			cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
			cairo_move_to(cr, 0, -5);
			cairo_rel_line_to(cr, 0, -15);
			cairo_move_to(cr, 0, -17);
			cairo_rel_line_to(cr, -7, 7);
			cairo_move_to(cr, 0, -17);
			cairo_rel_line_to(cr, 7, 7);
			cairo_stroke(cr);
			cairo_set_line_width(cr, 3);
			cairo_move_to(cr, 0, -5);
			cairo_rel_line_to(cr, -4, 3);
			cairo_move_to(cr, 0, -5);
			cairo_rel_line_to(cr, 4, 3);
			cairo_stroke(cr);
		//----------------------------------------------------------//

			if(sys.trk_timer_flag == B_TRUE)
			{
				cairo_set_source_rgb(cr, WHITE_RGB(scr));
				dashes[0] = 3;
				dashes[1] = 3;
				cairo_set_line_width(cr, 2);
				cairo_save(cr);
				cairo_move_to(cr, 0, -20);
				cairo_rotate(cr, DEG2RAD(sys.trk));
				cairo_rel_line_to(cr, 0, -WXR_RES_Y + 20);
				cairo_stroke(cr);
				cairo_restore(cr);

			//            snprintf(buf, sizeof (buf), "%2u\u00B0", ABS(sys.trk));
			//            align_text(cr, buf, -WXR_RES_X / 2 + FONT_SZ*2, -WXR_RES_Y - TOP_OFFSET*2 +
			//            LINE_HEIGHT,
			//                TEXT_ALIGN_RIGHT);
			//            cairo_show_text(cr, buf);
			//----------------------------------------------------------//

				cairo_set_line_width(cr, 2);
				cairo_set_source_rgb(cr, WHITE_RGB(scr));
				cairo_rectangle(cr, WXR_RES_X/2 - 51, -53 , 50, 14);
				cairo_stroke_preserve(cr);
				cairo_set_source_rgb(cr, GRAY_RGB(scr));
				cairo_fill(cr);
				cairo_set_source_rgb(cr, WHITE_RGB(scr));
				cairo_set_font_size(cr, 10);
				snprintf(buf, sizeof (buf), "Azim");
				align_text(cr, buf, WXR_RES_X/2 - 48, -47, TEXT_ALIGN_LEFT);
				cairo_show_text(cr, buf);

				cairo_set_source_rgb(cr, WHITE_RGB(scr));
				cairo_set_font_size(cr, FONT_SZ/2);
				snprintf(buf, sizeof (buf), "%+i", sys.trk);
				align_text(cr, buf, WXR_RES_X/2 - strlen(buf)/2 - 2 , -47, TEXT_ALIGN_RIGHT);
				cairo_show_text(cr, buf);
		//----------------------------------------------------------//
			}

			cairo_set_dash(cr, NULL, 0, 0);
			cairo_stroke(cr);
	//----------------------------------------------------------//
			cairo_set_line_width(cr, 2);
			dashes[0] = 2;
			dashes[1] = 10;
			cairo_set_dash(cr, dashes, 2, 0);
			cairo_set_font_size(cr, FONT_SZ);
			
			for (int i = 0; i < 4; i++) 
			{		
				cairo_set_source_rgb(cr, CYAN_RGB(scr));
				cairo_arc(cr, 0, -20, ((WXR_RES_Y - 20) / 4) * (i + 1), DEG2RAD(210), DEG2RAD(330));
				cairo_stroke(cr);
				if(i < 3)
				{
					double r = ((WXR_RES_Y - 20) / 4) * (i + 1);
					double ang = DEG2RAD(-120);
					double x = r*sin(ang);
					double y = r*cos(ang);
					cairo_set_source_rgb(cr, BLUE_RGB(scr));
					snprintf(buf, sizeof (buf), "%.3g", MET2NM(sys.range)/4*(i+1));
					align_text(cr, buf, x - strlen(buf)/2, y - FONT_SZ/4, TEXT_ALIGN_CENTER); // + strlen(buf)/2
					cairo_show_text(cr, buf);
					cairo_stroke(cr);
				}

			}
			cairo_set_dash(cr, NULL, 0, 0);

		//        cairo_set_line_width(cr, 2);
		//        cairo_set_source_rgb(cr, WHITE_RGB(scr));
		//        cairo_rectangle(cr, -WXR_RES_X/2+1, -WXR_RES_Y/2 - 15, 38, 25);
		//        cairo_stroke_preserve(cr);
		//        cairo_set_source_rgb(cr, GRAY_RGB(scr));
		//        cairo_fill(cr);
		//        cairo_set_font_size(cr, 12);
		//        cairo_set_source_rgb(cr, WHITE_RGB(scr));
		//        snprintf(buf, sizeof (buf), "NO");
		//        align_text(cr, buf, -WXR_RES_X/2+3 , -WXR_RES_Y/2 - 10 , TEXT_ALIGN_LEFT);
		//        cairo_show_text(cr, buf);
		//        snprintf(buf, sizeof (buf), "TAWS");
		//        align_text(cr, buf, -WXR_RES_X/2+3, -WXR_RES_Y/2 + 4, TEXT_ALIGN_LEFT);
		//        cairo_show_text(cr, buf);
		//----------------------------------------------------------//

		//----------------------------------------------------------//
			cairo_set_line_width(cr, 2);
			cairo_set_source_rgb(cr, WHITE_RGB(scr));
			cairo_rectangle(cr, -WXR_RES_X/2 + 1, -WXR_RES_Y + 70, 30, 30);
			cairo_stroke_preserve(cr);
			cairo_set_source_rgb(cr, GRAY_RGB(scr));
			cairo_fill(cr);
			cairo_set_source_rgb(cr, WHITE_RGB(scr));
			cairo_set_font_size(cr, 15);
			snprintf(buf, sizeof (buf), "%3.0f", MET2NM(sys.range) );
			align_text(cr, buf, -WXR_RES_X/2 + 15, -WXR_RES_Y + 77, TEXT_ALIGN_CENTER);
			cairo_show_text(cr, buf);
			snprintf(buf, sizeof (buf), "nm");
			align_text(cr, buf, -WXR_RES_X/2 + 15, -WXR_RES_Y + 93, TEXT_ALIGN_CENTER);
			cairo_show_text(cr, buf);
			cairo_stroke(cr);
		//----------------------------------------------------------//
		//        cairo_set_source_rgb(cr, WHITE_RGB(scr));
		//        cairo_rectangle(cr, WXR_RES_X/2 - 41, -WXR_RES_Y/3, 40, 20);
		//        cairo_stroke_preserve(cr);
		//        cairo_set_source_rgb(cr, GRAY_RGB(scr));
		//        cairo_fill(cr);
		//        cairo_set_source_rgb(cr, WHITE_RGB(scr));
		//        cairo_set_font_size(cr, 15);
		//        snprintf(buf, sizeof (buf), "TCAS");
		//        align_text(cr, buf, WXR_RES_X/2 - 2, -WXR_RES_Y/3 + 9, TEXT_ALIGN_RIGHT);
		//        cairo_show_text(cr, buf);
		//----------------------------------------------------------//
			cairo_set_source_rgb(cr, BLUE_RGB(scr));
			cairo_set_font_size(cr, 15);

			snprintf(buf, sizeof (buf), "%3.0f\u00B0", dr_getf(&drs.hdg) );
			cairo_text_extents(cr, buf, &tx);
			cairo_rectangle(cr, -tx.width/2 - 2, -WXR_RES_Y + tx.height/2, tx.x_advance + 2, tx.height*1.2);
			cairo_set_source_rgb(cr, 0, 0, 0);
			cairo_fill(cr);
			cairo_set_source_rgb(cr, BLUE_RGB(scr));
			align_text(cr, buf, 0, -WXR_RES_Y + tx.height, TEXT_ALIGN_CENTER);
			cairo_show_text(cr, buf);
			cairo_stroke(cr);
	/* 			snprintf(buf, sizeof (buf), "%.3g", MET2NM(sys.range - (sys.range/4)) );
				cairo_text_extents(cr, buf, &tx);

				cairo_rectangle(cr, -WXR_RES_X / 2, -104, tx.width, tx.height*1.5);
				cairo_set_source_rgb(cr, 0, 0, 0);
				cairo_fill(cr);
				cairo_set_source_rgb(cr, BLUE_RGB(scr));

				align_text(cr, buf, -WXR_RES_X / 2, -95
					, TEXT_ALIGN_LEFT);
				cairo_show_text(cr, buf);

				snprintf(buf, sizeof (buf), "%2.0f", MET2NM(sys.range - (sys.range/4)*2 ) );
				cairo_text_extents(cr, buf, &tx);
				cairo_rectangle(cr, -WXR_RES_X / 3 - tx.width/2, -74, tx.width, tx.height*1.5);
				cairo_set_source_rgb(cr, 0, 0, 0);
				cairo_fill(cr);
				cairo_set_source_rgb(cr, BLUE_RGB(scr));
				align_text(cr, buf, -WXR_RES_X / 3, -65
					, TEXT_ALIGN_CENTER);
				cairo_show_text(cr, buf);


				snprintf(buf, sizeof (buf), "%.3g", MET2NM(sys.range - (sys.range/4)*3 ) );
				cairo_text_extents(cr, buf, &tx);
				cairo_rectangle(cr, -WXR_RES_X / 6 - tx.width/2, -44, tx.width, tx.height*1.5);
				cairo_set_source_rgb(cr, 0, 0, 0);
				cairo_fill(cr);
				cairo_set_source_rgb(cr, BLUE_RGB(scr));
				align_text(cr, buf, -WXR_RES_X / 6, -35
					, TEXT_ALIGN_CENTER);
				cairo_show_text(cr, buf); */


			mutex_enter(&sys.mode_lock);
			strlcpy(mode_name, sys.aux[sys.cur_mode].name, sizeof (mode_name));
			mutex_exit(&sys.mode_lock);

			cairo_set_line_width(cr, 2);
			cairo_set_source_rgb(cr, WHITE_RGB(scr));
			cairo_rectangle(cr, WXR_RES_X/2 - 61, -19 , 60, 18);
			cairo_stroke_preserve(cr);
			cairo_set_source_rgb(cr, GRAY_RGB(scr));
			cairo_fill(cr);
			cairo_set_source_rgb(cr, WHITE_RGB(scr));
			cairo_set_font_size(cr, 15);
			align_text(cr, mode_name, WXR_RES_X/2 - 30, -10, TEXT_ALIGN_CENTER);
			cairo_show_text(cr, mode_name);
		//----------------------------------------------------------//
		//
		//        align_text(cr, mode_name, WXR_RES_X / 2, -20 , TEXT_ALIGN_RIGHT);
		//        cairo_show_text(cr, mode_name);

			if (wxr != NULL && sys.cur_mode > 0) 
			{

				vect3_t pos;
				vect2_t lim;

		//----------------------------------------------------------//
				cairo_set_line_width(cr, 2);
				cairo_set_source_rgb(cr, WHITE_RGB(scr));
				cairo_rectangle(cr, WXR_RES_X/2 - 103, -19 , 41, 18);
				cairo_stroke_preserve(cr);
				cairo_set_source_rgb(cr, GRAY_RGB(scr));
				cairo_fill(cr);
				cairo_set_source_rgb(cr, WHITE_RGB(scr));
				cairo_set_font_size(cr, 15);
				snprintf(buf, sizeof (buf), "WXr");
				align_text(cr, buf, WXR_RES_X/2 - 97, -10, TEXT_ALIGN_LEFT);
				cairo_show_text(cr, buf);

		//----------------------------------------------------------//

		//----------------------------------------------------------//
				if (strcmp(mode_name,"MAP") == 0) 
				{
		//----------------------------------------------------------//
					cairo_set_line_width(cr, 2);
					cairo_rectangle(cr, -WXR_RES_X/3.8, -15 , 70, 14);
					cairo_stroke_preserve(cr);
					cairo_set_source_rgb(cr, GRAY_RGB(scr));
					cairo_fill(cr);

					cairo_rectangle(cr, -WXR_RES_X/3.8, -15 , 70*sys.sepn, 14);
					cairo_set_source_rgb(cr, BLUE_RGB(scr));
					cairo_fill(cr);
					cairo_set_source_rgb(cr, WHITE_RGB(scr));
					cairo_set_font_size(cr, 12);
					snprintf(buf, sizeof (buf), "Separation");
					align_text(cr, buf, -WXR_RES_X/3.8 + 2, -8, TEXT_ALIGN_LEFT);
					cairo_show_text(cr, buf);
		//----------------------------------------------------------//
		//----------------------------------------------------------//
					cairo_set_line_width(cr, 2);
					cairo_set_source_rgb(cr, WHITE_RGB(scr));
					cairo_rectangle(cr, -WXR_RES_X/2+1, -15 , 70, 14);
					cairo_stroke_preserve(cr);
					cairo_set_source_rgb(cr, GRAY_RGB(scr));
					cairo_fill(cr);
					cairo_rectangle(cr, -WXR_RES_X/2+1, -15 , 70*(wxr_intf->get_gain(wxr)-0.5), 14);
					cairo_set_source_rgb(cr, BLUE_RGB(scr));
					cairo_fill(cr);
					cairo_set_source_rgb(cr, WHITE_RGB(scr));
					cairo_set_font_size(cr, 12);
					snprintf(buf, sizeof (buf), "Gain");
					align_text(cr, buf, -WXR_RES_X/2 + 20, -9, TEXT_ALIGN_LEFT);
					cairo_show_text(cr, buf);
		//----------------------------------------------------------//
				}
				else
				{
					if(wxr_intf->get_gain(wxr) != DFL_GAIN)
					{
						cairo_set_line_width(cr, 2);
						cairo_set_source_rgb(cr, WHITE_RGB(scr));
						cairo_rectangle(cr, -WXR_RES_X/2+1, -15 , 70, 14);
						cairo_stroke_preserve(cr);
						cairo_set_source_rgb(cr, GRAY_RGB(scr));
						cairo_fill(cr);
						cairo_rectangle(cr, -WXR_RES_X/2+1, -15 , 70*(wxr_intf->get_gain(wxr)-0.5), 14);
						cairo_set_source_rgb(cr, LIGHT_BLUE_RGB(scr));
						cairo_fill(cr);
						cairo_set_source_rgb(cr, WHITE_RGB(scr));
						cairo_set_font_size(cr, 12);
						snprintf(buf, sizeof (buf), "Gain");
						align_text(cr, buf, -WXR_RES_X/2 + 20, -9, TEXT_ALIGN_LEFT);
						cairo_show_text(cr, buf);
					}
				}
		//----------------------------------------------------------//
				cairo_set_line_width(cr, 2);
				cairo_set_source_rgb(cr, WHITE_RGB(scr));
				cairo_rectangle(cr, WXR_RES_X/2 - 61, -36 , 60, 14);
				cairo_stroke_preserve(cr);
				cairo_set_source_rgb(cr, GRAY_RGB(scr));
				cairo_fill(cr);
				cairo_set_source_rgb(cr, WHITE_RGB(scr));
				cairo_set_font_size(cr, 10);
				snprintf(buf, sizeof (buf), "Tilt");
				align_text(cr, buf, WXR_RES_X/2 - 59, -29, TEXT_ALIGN_LEFT);
				cairo_show_text(cr, buf);

				cairo_set_source_rgb(cr, WHITE_RGB(scr));
				cairo_set_font_size(cr, 10);
				snprintf(buf, sizeof (buf), "%+2.2f", sys.tilt);
				align_text(cr, buf, WXR_RES_X/2 - strlen(buf)/2 - 2 , -29, TEXT_ALIGN_RIGHT);
				cairo_show_text(cr, buf);
		//----------------------------------------------------------//

		//            snprintf(buf, sizeof (buf), "%1.1f", wxr_intf->get_gain(wxr));
		//            align_text(cr, buf, -WXR_RES_X / 2, -20,
		//                TEXT_ALIGN_LEFT);
		//            cairo_show_text(cr, buf);
				wxr_intf->get_acf_pos(wxr, &NULL_GEO_POS3, &pos);
				wxr_intf->get_stab(wxr, &lim.x, &lim.y);

				if(lim.x == 0 || lim.y == 0)
				{
		//----------------------------------------------------------//
					cairo_set_line_width(cr, 2);
					cairo_rectangle(cr, WXR_RES_X/2 - 36, -140 , 35, 30);
					cairo_stroke_preserve(cr);
					cairo_set_source_rgb(cr, GRAY_RGB(scr));
					cairo_fill(cr);
					cairo_set_source_rgb(cr, WHITE_RGB(scr));
					cairo_set_font_size(cr, 12);
					snprintf(buf, sizeof (buf), "Stab.");
					align_text(cr, buf, WXR_RES_X/2 - 2, -133, TEXT_ALIGN_RIGHT);
					cairo_show_text(cr, buf);
					snprintf(buf, sizeof (buf), "Off");
					align_text(cr, buf, WXR_RES_X/2 - 2, -118, TEXT_ALIGN_RIGHT);
					cairo_show_text(cr, buf);
		//----------------------------------------------------------//
				}
				else if(ABS(pos.x) > lim.x || ABS(pos.z) > lim.y)
				{
		//----------------------------------------------------------//
					cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
					cairo_set_source_rgb(cr, 1, 0, 0);
					cairo_set_font_size(cr, 20);
					snprintf(buf, sizeof (buf), "STAB LIMIT");
					align_text(cr, buf, 0, -WXR_RES_Y/2, TEXT_ALIGN_CENTER);
					cairo_show_text(cr, buf);
		//----------------------------------------------------------//
				}
			}

		       break;
	    	default:
		        break;
    		}
	}
}

static void
render_cb(cairo_t *cr, unsigned w, unsigned h, void *userinfo)
{
	wxr_scr_t *scr = userinfo;

	cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
	cairo_paint(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

	cairo_save(cr);

	cairo_scale(cr, w / (double)WXR_RES_X, h / (double)WXR_RES_Y);
	cairo_translate(cr, WXR_RES_X / 2, WXR_RES_Y);
	cairo_scale(cr, scr->underscan, scr->underscan);

	render_ui(cr, scr);

	cairo_restore(cr);

	if (scr->power < 0.99) {
		cairo_set_source_rgba(cr, 0, 0, 0, 1.0 - scr->power);
		cairo_paint(cr);
	}
}

static void
parse_conf_file(const conf_t *conf)
{
	const char *str;
	if(conf_get_i(conf, "cdref_int/num", (int *)&cdrs.num_int)){
        cdrs.num_int = clampi(cdrs.num_int, 0, 16);
        for(unsigned i = 0; i < cdrs.num_int; i++){
            if (conf_get_str_v(conf, "cdrint/%d/", &str, i)){
                dr_create_i(&cdrs.drefs_int[i], &cdrs.dref_val_int[i], B_TRUE, str, i);
                conf_get_i_v(conf, "cdrint/%d/val", (int *)&cdrs.dref_val_int[i], i);
            }
        }
	}
	if(conf_get_i(conf, "cdref_float/num", (int *)&cdrs.num_float)){
        cdrs.num_float = clampi(cdrs.num_float, 0, 16);
        for(unsigned i = 0; i < cdrs.num_float; i++){
            if (conf_get_str_v(conf, "cdrfloat/%d/", &str, i)){
                dr_create_f64(&cdrs.drefs_float[i], &cdrs.dref_val_float[i], B_TRUE, str, i);
                conf_get_d_v(conf, "cdrfloat/%d/val", &cdrs.dref_val_float[i], i);
            }
        }
	}

	conf_get_i(conf, "efis/x", (int *)&sys.efis_xywh[0]);
	conf_get_i(conf, "efis/y", (int *)&sys.efis_xywh[1]);
	sys.efis_xywh[0] = clampi(sys.efis_xywh[0] + EFIS_OFF_X, 0, 2048);
	sys.efis_xywh[1] = clampi(sys.efis_xywh[1] + EFIS_OFF_Y, 0, 2048);
	sys.efis_xywh[2] = EFIS_WIDTH;
	sys.efis_xywh[3] = EFIS_HEIGHT;

	conf_get_i(conf, "num_modes", (int *)&sys.num_modes);
	sys.num_modes = clampi(sys.num_modes, 0, MAX_MODES);

	for (unsigned i = 0; i < sys.num_modes; i++) {
		wxr_conf_t *mode = &sys.modes[i];
		mode_aux_info_t *aux = &sys.aux[i];

		conf_get_i(conf, "res/x", (int *)&mode->res_x);
		conf_get_i(conf, "res/y", (int *)&mode->res_y);
		mode->res_x = clampi(mode->res_x, 64, 512);
		mode->res_y = clampi(mode->res_y, 64, 512);

		conf_get_d_v(conf, "mode/%d/beam_shape/x",
		    &mode->beam_shape.x, i);
		conf_get_d_v(conf, "mode/%d/beam_shape/y",
		    &mode->beam_shape.y, i);
		mode->beam_shape.x = clamp(mode->beam_shape.x, 1, 90);
		mode->beam_shape.y = clamp(mode->beam_shape.y, 1, 90);

		conf_get_d_v(conf, "mode/%d/scan_time",
		    &mode->scan_time, i);
		mode->scan_time = clamp(mode->scan_time, 0.1, 100);

		conf_get_d_v(conf, "mode/%d/scan_angle",
		    &mode->scan_angle, i);
		mode->scan_angle = clamp(mode->scan_angle, 1, 180);

		conf_get_d_v(conf, "mode/%d/scan_angle_vert",
		    &mode->scan_angle_vert, i);
		mode->scan_angle_vert = clamp(mode->scan_angle_vert, 1, 180);

		conf_get_d_v(conf, "mode/%d/smear/x", &mode->smear.x, i);
		conf_get_d_v(conf, "mode/%d/smear/y", &mode->smear.y, i);
		mode->smear.x = clamp(mode->smear.x, 0, 100);
		mode->smear.y = clamp(mode->smear.y, 0, 100);

		conf_get_d_v(conf, "mode/%d/parked_azi",
		    &mode->parked_azi, i);
		mode->parked_azi = clamp(mode->parked_azi,
		    -mode->scan_angle / 2, mode->scan_angle / 2);

		conf_get_i(conf, "num_ranges", (int *)&mode->num_ranges);
		mode->num_ranges = clampi(mode->num_ranges, 0, WXR_MAX_RANGES);
		for (unsigned j = 0; j < mode->num_ranges; j++)
			conf_get_d_v(conf, "range/%d", &mode->ranges[j], j);

		conf_get_d_v(conf, "mode/%d/stab_lim/x", &aux->stab_lim.x,
		    i);
		conf_get_d_v(conf, "mode/%d/stab_lim/y", &aux->stab_lim.y,
		    i);

		conf_get_i_v(conf, "mode/%d/num_colors",
		    (int *)&aux->num_colors, i);
		aux->num_colors = MIN(aux->num_colors, MAX_COLORS);
		for (unsigned j = 0; j < aux->num_colors; j++) {
			conf_get_d_v(conf, "mode/%d/colors/%d/thresh",
			    &aux->colors[j].min_val, i, j);
			if (conf_get_str_v(conf, "mode/%d/colors/%d/rgba",
			    &str, i, j)) {
				(void) sscanf(str, "%x", &aux->colors[j].rgba);
				aux->colors[j].rgba = BE32(aux->colors[j].rgba);
			}
			/* back up the master color palette */
			memcpy(aux->base_colors, aux->colors,
			    sizeof (aux->base_colors));
		}

		if (conf_get_str_v(conf, "mode/%d/name", &str, i))
			strlcpy(aux->name, str, sizeof (aux->name));

        if(!conf_get_b_v(conf, "mode/%d/is_wxr", &aux->is_wxr, i))
            aux->is_wxr = B_TRUE;
        if(!conf_get_b_v(conf, "mode/%d/is_stby", &mode->is_stby, i))
            mode->is_stby = B_FALSE;
        if(!conf_get_b_v(conf, "mode/%d/is_alert", &mode->is_alert, i))
            mode->is_alert = B_FALSE;
	}

	if (conf_get_str(conf, "power_dr", &str))
		strlcpy(sys.power_dr.name, str, sizeof (sys.power_dr.name));
	if (conf_get_str(conf, "power_sw_dr", &str)) {
		strlcpy(sys.power_sw_dr.name, str,
		    sizeof (sys.power_sw_dr.name));
	}
	conf_get_d(conf, "power_on_delay", &sys.power_on_delay);
	if (conf_get_str(conf, "range_dr", &str))
		strlcpy(sys.range_dr.name, str, sizeof (sys.range_dr.name));
	if (conf_get_str(conf, "tilt_dr", &str))
		strlcpy(sys.tilt_dr.name, str, sizeof (sys.tilt_dr.name));
	if (conf_get_str(conf, "mode_dr", &str))
		strlcpy(sys.mode_dr.name, str, sizeof (sys.mode_dr.name));
	if (conf_get_str(conf, "gain_dr", &str))
		strlcpy(sys.gain_dr.name, str, sizeof (sys.gain_dr.name));
	if (conf_get_str(conf, "sepn_dr", &str))
		strlcpy(sys.sepn_dr.name, str, sizeof (sys.sepn_dr.name));
    if (conf_get_str(conf, "trk_dr", &str))
		strlcpy(sys.trk_dr.name, str, sizeof (sys.trk_dr.name));
    if (conf_get_str(conf, "stab_dr", &str))
		strlcpy(sys.stab_dr.name, str, sizeof (sys.stab_dr.name));
    if (conf_get_str(conf, "nav_dr", &str))
		strlcpy(sys.nav_dr.name, str, sizeof (sys.nav_dr.name));
	else sys.nav = 0;
    if (conf_get_str(conf, "vp_dr", &str))
		strlcpy(sys.vp_dr.name, str, sizeof (sys.vp_dr.name));
	else sys.vp = 0;
	conf_get_d(conf, "gain_auto_pos", &sys.gain_auto_pos);
	conf_get_d(conf, "alert_rate", &sys.alert_rate);
	sys.alert_rate = MAX(sys.alert_rate, 1);
	conf_get_d(conf, "tilt_rate", &sys.tilt_rate);
	sys.tilt_rate = MAX(sys.tilt_rate, 1);
	conf_get_b(conf, "shadow_enable", &sys.shadow_enable);

	conf_get_d(conf, "ctl/delay/power_sw", &sys.power_sw_ctl.delay);
	conf_get_d(conf, "ctl/delay/mode", &sys.mode_ctl.delay);
	conf_get_d(conf, "ctl/delay/tilt", &sys.tilt_ctl.delay);
	conf_get_d(conf, "ctl/delay/range", &sys.range_ctl.delay);

	conf_get_i(conf, "num_screens", (int *)&sys.num_screens);
	sys.num_screens = clampi(sys.num_screens, 0, MAX_SCREENS);

    if (conf_get_i(conf, "ui/style", (int *)&sys.ui_style))
	sys.ui_style = clampi(sys.ui_style, 0, 3);
	else sys.ui_style = 0;
	for (unsigned i = 0; i < sys.num_screens; i++) {
		wxr_scr_t *scr = &sys.screens[i];
		//const char *str;

		conf_get_d_v(conf, "scr/%d/x", &scr->x, i);
		conf_get_d_v(conf, "scr/%d/y", &scr->y, i);
		conf_get_d_v(conf, "scr/%d/w", &scr->w, i);
		conf_get_d_v(conf, "scr/%d/h", &scr->h, i);

		conf_get_d_v(conf, "scr/%d/fps", &scr->fps, i);
		scr->fps = clamp(scr->fps, 0.1, 100);

		conf_get_d_v(conf, "ctl/delay/scr/%d/power_sw",
		    &scr->power_sw_ctl.delay, i);

		scr->underscan = 1.0;
		conf_get_d_v(conf, "scr/%d/underscan", &scr->underscan, i);

		if (conf_get_str_v(conf, "scr/%d/power_dr", &str, i)) {
			strlcpy(scr->power_dr.name, str,
			    sizeof (scr->power_dr.name));
		}
		if (conf_get_str_v(conf, "scr/%d/power_sw_dr", &str, i)) {
			strlcpy(scr->power_sw_dr.name, str,
			    sizeof (scr->power_sw_dr.name));
		}
		conf_get_d_v(conf, "scr/%d/power_on_rate",
		    &scr->power_on_rate, i);
		conf_get_d_v(conf, "scr/%d/power_off_rate",
		    &scr->power_off_rate, i);
		scr->power_on_rate = MAX(scr->power_on_rate, 0.05);
		scr->power_off_rate = MAX(scr->power_off_rate, 0.05);

		scr->mtcr = mt_cairo_render_init(scr->w, scr->h, scr->fps,
		    NULL, render_cb, NULL, scr);
		scr->sys = &sys;

		if (conf_get_str_v(conf, "scr/%d/brt_dr", &str, i)) {
			strlcpy(scr->brt_dr.name, str,
			    sizeof (scr->brt_dr.name));
		}

		scr->rvoffset = 0.0;
		conf_get_d_v(conf, "scr/%d/rvoff", &scr->rvoffset, i);

		scr->voffset = 0.0;
		conf_get_d_v(conf, "scr/%d/voff", &scr->voffset, i);

		scr->hrat = 1.0;
		conf_get_d_v(conf, "scr/%d/hrat", &scr->hrat, i);

		scr->scale = 1.0;
		conf_get_d_v(conf, "scr/%d/scale", &scr->scale, i);

		scr->vert_mode_scale = 1.0;
		conf_get_d_v(conf, "scr/%d/vert_mode_scale", &scr->vert_mode_scale, i);
	}
}

bool_t get_mode()
{
    return sys.aux[sys.cur_mode].is_wxr;
}

bool_t
sa_init(const conf_t *conf)
{
	XPLMPluginID opengpws;

	ASSERT(!inited);
	inited = B_TRUE;

	memset(&sys, 0, sizeof (sys));
	parse_conf_file(conf);
	mutex_init(&sys.mode_lock);

	open_debug_cmd = XPLMCreateCommand("openwxr/standalone_window",
	    "Open OpenWXR standalone mode debug window");
	ASSERT(open_debug_cmd != NULL);
	XPLMRegisterCommandHandler(open_debug_cmd, open_debug_win, 0, NULL);

	opengpws = XPLMFindPluginBySignature(OPENGPWS_PLUGIN_SIG);
	if (opengpws == XPLM_NO_PLUGIN_ID) {
		logMsg("WXR init failure: OpenGPWS plugin not found. "
		    "Is it installed?");
		goto errout;
	}
	XPLMSendMessageToPlugin(opengpws, EGPWS_GET_INTF, &sys.terr);

	openwxr = XPLMFindPluginBySignature(OPENWXR_PLUGIN_SIG);
	ASSERT(openwxr != XPLM_NO_PLUGIN_ID);

	XPLMSendMessageToPlugin(openwxr, OPENWXR_INTF_GET, &wxr_intf);
	ASSERT(wxr_intf != NULL);
	XPLMSendMessageToPlugin(openwxr, OPENWXR_ATMO_GET, &atmo);
	ASSERT(atmo != NULL);
	XPLMSendMessageToPlugin(openwxr, OPENWXR_ATMO_XP11_SET_EFIS,
	    sys.efis_xywh);

	fdr_find(&drs.lat, "sim/flightmodel/position/latitude");
	fdr_find(&drs.lon, "sim/flightmodel/position/longitude");
	fdr_find(&drs.elev, "sim/flightmodel/position/elevation");
	fdr_find(&drs.sim_time, "sim/time/total_running_time_sec");
	fdr_find(&drs.panel_render_type, "sim/graphics/view/panel_render_type");
	fdr_find(&drs.hdg, "sim/flightmodel/position/psi");
	fdr_find(&drs.pitch, "sim/flightmodel/position/theta");
	fdr_find(&drs.roll, "sim/flightmodel/position/phi");

	XPLMRegisterFlightLoopCallback(floop_cb, -1, NULL);
	XPLMRegisterFlightLoopCallback(trk_timer_cb, 0, NULL);
	XPLMRegisterFlightLoopCallback(alert_timer_cb, 0, NULL);
	XPLMRegisterDrawCallback(draw_cb, xplm_Phase_Gauges, 0, NULL);//xplm_Phase_Panel

	return (B_TRUE);
errout:
	sa_fini();
	return (B_FALSE);
}

void
sa_fini(void)
{
	if (!inited)
		return;
	inited = B_FALSE;

	XPLMUnregisterCommandHandler(open_debug_cmd, open_debug_win, 0, NULL);

	if(cdrs.num_int > 0){
		for(unsigned i = 0; i < cdrs.num_int; i++){
			dr_delete(&cdrs.drefs_int[i]);
		}
	}
	if(cdrs.num_float > 0){
		for(unsigned i = 0; i < cdrs.num_float; i++){
			dr_delete(&cdrs.drefs_float[i]);
		}
	}

	for (unsigned i = 0; i < sys.num_screens; i++) {
		if (sys.screens[i].mtcr != NULL)
			mt_cairo_render_fini(sys.screens[i].mtcr);
	}

	if (wxr != NULL) {
		wxr_intf->fini(wxr);
		wxr = NULL;
	}
	wxr_intf = NULL;
	atmo = NULL;

	if (debug_win != NULL) {
		XPLMDestroyWindow(debug_win);
		debug_win = NULL;
	}

	XPLMUnregisterFlightLoopCallback(floop_cb, NULL);
	XPLMUnregisterFlightLoopCallback(trk_timer_cb, NULL);
	XPLMUnregisterFlightLoopCallback(alert_timer_cb, NULL);
	XPLMUnregisterDrawCallback(draw_cb, xplm_Phase_Gauges, 0, NULL);

	mutex_destroy(&sys.mode_lock);
}

