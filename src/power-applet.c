/* 
 * Copyright (C) 2001 Free Software Foundation
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Authors : Eskil Heyn Olsen <eskil@eskil.dk>
 */

#include <config.h>

#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <ctype.h>
#include <fcntl.h>
#include <errno.h>
#include <math.h>
#include <dirent.h>

#include <gnome.h>
#include <libgnomeui/gnome-window-icon.h>
#include <applet-widget.h>
#include <glade/glade.h>
#include <eel/eel-image.h>
#include <eel/eel-label.h>
#include <eel/eel-gdk-pixbuf-extensions.h>

#include <powerm.h>

#define CFG_SHOW_PERCENT "FALSE"
#define CFG_SHOW_TIME "TRUE"
#define CFG_SHOW_AC "FALSE"
#define CFG_SHOW_FULL_DIALOG "FALSE"
#define CFG_SHOW_LOW_DIALOG "TRUE"
#define CFG_LOW_THRESHOLD "5"
#define CFG_SHUTDOWN_THRESHOLD "2"
#define CFG_UPDATE_INTERVAL "2"
#define CFG_TEXT_AA "TRUE"
#define CFG_FLASH_TEXT_RED "FALSE"
#define CFG_THEME "default"

typedef struct {
	gchar *theme;
	gboolean show_percent;
	gboolean show_time;
	gboolean show_ac;
	gboolean show_full_dialog, show_low_dialog;
	gint low_threshold;
	gint shutdown_threshold;
	gint update_interval;
	gint suspend_threshold;
	gboolean text_aa;
	gboolean flash_red_when_low;

	gint text_smaller;
	gint smooth_font_size;
	GList *themes;

	/* contains a glist of char*, pointing to available images for the current theme */
	GList *images;
	/* contains a list of char*, pointing to the themes lowXX images (if any) */
	GList *low_images;
	/* contains pointers into the images GList. 0-100 are for battery, 101-201 are for AC */
	char *pixmaps[202];
	/* pointer to the current used file name */
	char *current_pixmap;

	/* Current state of the app */
	gboolean flashing;
	gboolean message_already_showed;
	gint old_minutes;
	gint old_ac;
	PowerInfo info;
} Properties;

/* What extensions do we try to load */
char *pixmap_extensions[] = 
{
	".png",
	".xpm",
	NULL
};

static GtkWidget *global_property_box = NULL;
static GtkWidget *global_applet = NULL;
static GtkWidget *glade_property_box = NULL;
static GladeXML *xml = NULL;
static gchar* glade_file=NULL;

static void show_error_dialog (gchar*,...);
static void show_warning_dialog (gchar*,...);
static void show_message_dialog (gchar*,...);

static int power_applet_timeout_handler (GtkWidget *applet);

/******************************************************************/

static void
power_applet_panel_do_draw_signal (GtkWidget *applet, gpointer data) {	
	guchar *rgb;
	int w, h, rowstride;
	GdkPixbuf *pixbuf;
	GdkColor colour;
	EelImage *pixmap;
	EelLabel *label;

	pixmap = EEL_IMAGE (gtk_object_get_data (GTK_OBJECT (applet), "pixmap"));
	label = EEL_LABEL (gtk_object_get_data (GTK_OBJECT (applet), "percent-label"));

	applet_widget_get_rgb_bg (APPLET_WIDGET (applet), 
				  &rgb, &w, &h,
				  &rowstride);

	pixbuf = gdk_pixbuf_new_from_data (rgb, GDK_COLORSPACE_RGB, FALSE, 8,
					   w, h, rowstride, 
					   (GdkPixbufDestroyNotify)g_free, NULL);
	
	eel_gdk_pixbuf_average_value (pixbuf, &colour);

	/*
	if (colour.red+colour.green+colour.blue < 32768*3) {
		eel_label_set_text_color (label, 0xFFFFFF);
	} else {
		eel_label_set_text_color (label, 0x000000);
	}
	*/

  	eel_image_set_tile_pixbuf (pixmap, pixbuf);
	eel_label_set_tile_pixbuf (label, pixbuf);

	gdk_pixbuf_unref (pixbuf);
}

static gboolean
power_applet_do_label (GtkWidget *applet) {
	Properties *props;

	props = gtk_object_get_data (GTK_OBJECT (applet), "properties");
	return props->show_percent || props->show_time || props->show_ac;
}

static void 
power_applet_draw (GtkWidget *applet)
{
	GtkWidget *pixmap;
	GtkWidget *pct_label;
	Properties *props;

	props = gtk_object_get_data (GTK_OBJECT (applet), "properties");
	pct_label = GTK_WIDGET (gtk_object_get_data (GTK_OBJECT (applet), "percent-label"));
	pixmap = GTK_WIDGET (gtk_object_get_data (GTK_OBJECT (applet), "pixmap"));

	if (!GTK_WIDGET_REALIZED (pixmap)) {
		return;
	}

	/* Update the tooltip */
	/* g_message ("old min = %d, new min = %d", props->old_minutes, props->info.battery_time_left); */
	/* g_message ("message showed = %s", props->message_already_showed ? "TRUE" : "FALSE"); */

	/* let's not update the tooltip unless nessecary,
	   since, people won't have time to see it. */
	if (props->old_minutes != props->info.battery_time_left || props->old_ac != props->info.ac_online) {
		char *tmp; 
		int hr, min, r_hr, r_min;

		hr = props->info.battery_time_left/60;
		min = props->info.battery_time_left%60;
		r_hr = props->info.recharge_time_left/60;
		r_min = props->info.recharge_time_left%60;


		if (props->info.percent >= 0) {
			if (props->info.ac_online == 1) {
				if (props->info.percent < 100 && r_hr>=0 && r_min >= 0) {
					tmp = g_strdup_printf ("%3.2d%% %2.02d:%2.02d - AC (%2.02d:%2.02d to full charge)", 
							       props->info.percent, hr, min, r_hr, r_min);
				} else if (props->info.percent == 100) {
					tmp = g_strdup_printf ("%3.2d%% %2.02d:%2.02d - AC (full charge)", 
							       props->info.percent, hr, min);
				} else {
					tmp = g_strdup_printf ("%3.2d%% %2.02d:%2.02d - AC", 
							       props->info.percent, hr, min);
				}
			} else {
				tmp = g_strdup_printf ("%3.2d%% %2.02d:%2.02d", props->info.percent, hr, min);
			}
		} else {
			tmp = g_strdup_printf (_("N/A"));
		}
		applet_widget_set_tooltip (APPLET_WIDGET (applet),
					   tmp);

		g_free (tmp);
	}
	props->old_minutes = props->info.battery_time_left;
	props->old_ac = props->info.ac_online;

	/* update the text label */ 
	if (power_applet_do_label (applet)) {
		GString *str = g_string_new ("");
		if (props->info.percent >= 0) {
			if (props->show_percent) {
				g_string_sprintfa (str, "%.2d%%", props->info.percent);
			}
			
			if (props->show_time) {
				int hr, min;
				
				hr = props->info.battery_time_left/60;
				min = props->info.battery_time_left%60;

				if (str->len) {
					g_string_sprintfa (str, " ");
				} 
				g_string_sprintfa (str, "%.02d:%.02d", hr, min);			
			}

			if (props->show_ac && props->info.ac_online == 1) {
				if (str->len) {
					g_string_sprintfa (str, " - ");			
				} 
				g_string_sprintfa (str, "AC");			
			}
		} else {
			/* N/A for Not Available */
			g_string_append (str, _("N/A"));
		}
			
		eel_label_set_text (EEL_LABEL (pct_label), str->str);
		g_string_free (str, TRUE);
	}

	/* Update the image */
	if (props->info.percent >= 0 && props->info.percent <= 100) {
		int pixmap_offset = props->info.percent;

		if (props->info.ac_online == 1) {
			pixmap_offset += 100;
		}
		
		/* 
		g_message ("%p != %p = %s", 
			   props->pixmaps[pixmap_offset], props->current_pixmap, 
			   props->pixmaps[pixmap_offset] != props->current_pixmap ? "TRUE" : "FALSE");
		*/
		if (props->pixmaps[pixmap_offset] != props->current_pixmap) {
			props->current_pixmap = props->pixmaps[pixmap_offset];
			if ( !props->flashing) {
				eel_image_set_pixbuf_from_file_name (EEL_IMAGE (pixmap), props->current_pixmap);
			}
			/*
			g_message ("loading image %d = %s",
				   pixmap_offset,
				   props->current_pixmap);
			*/
		}
	} else {
		g_warning ("Weird pct = %d", props->info.percent);
	}
}

static gboolean 
power_applet_flash_timeout (GtkWidget *applet) {
	static gboolean foo = FALSE;
	GtkWidget *pct_label;

	pct_label = GTK_WIDGET (gtk_object_get_data (GTK_OBJECT (applet), "percent-label"));
	if (foo) {
		eel_label_set_text_color (EEL_LABEL (pct_label), 0xFF0000);
		foo = FALSE;
	} else {
		eel_label_set_text_color (EEL_LABEL (pct_label), 0x000000);
		foo = TRUE;
	}		
	return TRUE;
}

static gboolean
power_applet_animate_timeout (GtkWidget *applet) 
{	
	static int num = 0;
	Properties *properties;
	GtkWidget *pixmap;
	GList *image;

	properties = gtk_object_get_data (GTK_OBJECT (applet), "properties");
	pixmap = GTK_WIDGET (gtk_object_get_data (GTK_OBJECT (applet), "pixmap"));
	if (num >= g_list_length (properties->low_images)) {
		num = 0;
	}
	image = g_list_nth (properties->low_images, num);
	/*
	g_message ("animating %d of %d to %s", 
		   num, g_list_length (properties->low_images), (char*)image->data); 
	*/
	eel_image_set_pixbuf_from_file_name (EEL_IMAGE (pixmap), (char*)image->data);	
	num++;

	return TRUE;
}

static void
power_applet_start_animation (GtkWidget *applet) 
{
	GtkWidget *pixmap;
	guint timeout_handler_id;

	timeout_handler_id = gtk_timeout_add (500, 
					      (GtkFunction)power_applet_animate_timeout,
					      applet);
	pixmap = GTK_WIDGET (gtk_object_get_data (GTK_OBJECT (applet), "pixmap"));
	gtk_object_set_data (GTK_OBJECT (applet), 
			     "animate_timer", 
			     GINT_TO_POINTER (timeout_handler_id));
}

static void
power_applet_start_label_flashing (GtkWidget *applet) 
{
	guint timeout_handler_id;

	timeout_handler_id = gtk_timeout_add (500, 
					      (GtkFunction)power_applet_flash_timeout,
					      applet);
	gtk_object_set_data (GTK_OBJECT (applet), 
			     "flash_red_timer", 
			     GINT_TO_POINTER (timeout_handler_id));
}

static void
power_applet_reset_label (GtkWidget *applet) 
{
	GtkWidget *pct_label;
	guint timeout_handler_id = GPOINTER_TO_INT (gtk_object_get_data (GTK_OBJECT (applet),
									 "flash_red_timer"));
	
	gtk_timeout_remove (timeout_handler_id);
	
	pct_label = GTK_WIDGET (gtk_object_get_data (GTK_OBJECT (applet), "percent-label"));
	eel_label_set_text_color (EEL_LABEL (pct_label), 0x000000);
}

static void
power_applet_stop_animation (GtkWidget *applet) 
{
	GtkWidget *pixmap;
	guint timeout_handler_id = GPOINTER_TO_INT (gtk_object_get_data (GTK_OBJECT (applet),
									 "animate_timer"));
	Properties *properties;

	properties = gtk_object_get_data (GTK_OBJECT (applet), "properties");
	gtk_timeout_remove (timeout_handler_id);
	pixmap = GTK_WIDGET (gtk_object_get_data (GTK_OBJECT (applet), "pixmap"));
	eel_image_set_pixbuf_from_file_name (EEL_IMAGE (pixmap), properties->current_pixmap);	
}

static void
power_applet_low_power_state (GtkWidget *applet) 
{
	Properties *properties;

	properties = gtk_object_get_data (GTK_OBJECT (applet), "properties");

	/* if we're charging, cancel any special things */
	if (properties->info.ac_online == 1) {
		if (properties->flashing) {
			power_applet_reset_label (applet);
			power_applet_stop_animation (applet);
		}
		properties->flashing = FALSE;
	} else if (properties->flashing == FALSE) {
		if (properties->flash_red_when_low) {
			power_applet_start_label_flashing (applet);
		}
		power_applet_start_animation (applet);
		properties->flashing = TRUE;
	}

	if (properties->info.ac_online == 0 && 
	    properties->show_low_dialog && 
	    !properties->message_already_showed) {
		properties->message_already_showed = TRUE;
		g_message ("low dialog here");
		show_message_dialog (_("Battery charge is down to %d percent"), properties->low_threshold);
	}
}

static void
power_applet_full_power_state (GtkWidget *applet) 
{
	Properties *properties;

	properties = gtk_object_get_data (GTK_OBJECT (applet), "properties");

	if (properties->show_full_dialog && 
	    !properties->message_already_showed) {
		properties->message_already_showed = TRUE;
		g_message ("full dialog here");
		show_message_dialog (_("Battery fully charged"));
	}
}

static void
power_applet_update_state (GtkWidget *applet) 
{
	Properties *properties;

	properties = gtk_object_get_data (GTK_OBJECT (applet), "properties");

	if (properties->info.percent <= properties->low_threshold) {
		power_applet_low_power_state (applet);
	} else if (properties->info.percent == 100) {
		power_applet_full_power_state (applet);
	} else {
		if (properties->flashing) {
			power_applet_reset_label (applet);
			power_applet_stop_animation (applet);
		}
		properties->flashing = FALSE;
		properties->message_already_showed = FALSE;	
	}	
	power_applet_draw (applet);
}

static void
power_applet_load_theme_image (GtkWidget *applet, 
			       Properties *properties,
			       const char *themedir,
			       const char *filename) 
{
	int j;
	char *dot_pos = strrchr (filename, '.');

	/* Check the allowed extensions */
	for (j = 0; pixmap_extensions[j]; j++) {
		if (strcasecmp (dot_pos, pixmap_extensions[j])==0) { 
			int i;
			int pixmap_offset_begin = 0;
			int pixmap_offset_end = 0;
			char *dupe;
			gboolean check_range = FALSE;
			
			/* g_message ("Located theme image %s", filename); */
			if (strncmp (filename, "charging-", 9) == 0) {
				sscanf (filename, "charging-%d-%d.",
					&pixmap_offset_begin, &pixmap_offset_end);
				pixmap_offset_begin += 101;
				pixmap_offset_end += 101;
				check_range = TRUE;
			} else if (strncmp (filename, "battery-", 8) == 0) {
				sscanf (filename, "battery-%d-%d.",
					&pixmap_offset_begin, &pixmap_offset_end);
				check_range = TRUE;
			} else if (strncmp (filename, "low", 3) == 0) {
				properties->low_images = g_list_prepend (properties->low_images,
									 g_strdup_printf ("%s/%s", 
											  themedir, 
											  filename));
			} else {
				/* uhm, not charging or battery or low, but an 
				   image ? could be wrongly named */
				show_warning_dialog ("Theme %s has an odd image.\n"
						     "%s", 
						     properties->theme, filename);
			}

			if (check_range) {
				dupe = g_strdup_printf ("%s/%s", themedir, filename);
				properties->images = g_list_prepend (properties->images,
								     dupe);
				for (i = pixmap_offset_begin; i <= pixmap_offset_end; i++) {
					if (properties->pixmaps[i] != NULL) {
						show_warning_dialog ("Probable image overlap in\n"
								     "%s.", filename);
						
					} else {
						properties->pixmaps[i] = dupe;
					}
				}
			}

		}
	}	
}

static void
power_applet_load_theme (GtkWidget *applet) {
	Properties *properties;
	DIR *dir;
	struct dirent *dirent;
	char *pixmapdir;
	char *themedir;

	properties = gtk_object_get_data (GTK_OBJECT (applet), "properties");

	pixmapdir = gnome_unconditional_pixmap_file (PACKAGE);
	themedir = g_strdup_printf ("%s/%s", pixmapdir, properties->theme);
	dir = opendir (themedir);

	/* blank out */
	if (properties->images) {
		int j;
		g_list_foreach (properties->low_images, (GFunc)g_free, NULL);
		g_list_free (properties->low_images);
		properties->low_images = NULL;

		g_list_foreach (properties->images, (GFunc)g_free, NULL);
		g_list_free (properties->images);
		properties->images = NULL;
		for (j=0; j < 202; j++) {
			properties->pixmaps[j] = NULL;
		}
	}

	if (!dir) {
		show_error_dialog (_("No themes installed"));
	} else 
		while ((dirent = readdir (dir)) != NULL) {
			if (*dirent->d_name != '.') {
				if (strrchr (dirent->d_name, '.')!=NULL) {
					power_applet_load_theme_image (applet, 
								       properties,
								       themedir,
								       dirent->d_name);
				}
			}
		}

	if (properties->low_images && g_list_length (properties->low_images) > 1) {
		properties->low_images = g_list_sort (properties->low_images,
						      (GCompareFunc)g_strcasecmp);
	}

	g_free (pixmapdir);
	g_free (themedir);
}

static void
power_applet_set_theme (GtkWidget *applet, gchar *theme) {
	Properties *props = gtk_object_get_data (GTK_OBJECT (applet), "properties");

	g_free (props->theme);
	props->theme = g_strdup (theme);

	/* load the new images and update the gtk widgets */
	power_applet_load_theme (applet);
	power_applet_draw (global_applet);  
}

static void
power_applet_set_update_interval (GtkWidget *applet, int interval) {
	Properties *props = gtk_object_get_data (GTK_OBJECT (applet), "properties");
	guint timeout_handler_id = GPOINTER_TO_INT (gtk_object_get_data (GTK_OBJECT (applet), "timeout_handler_id"));

	props->update_interval = interval;
	gtk_timeout_remove (timeout_handler_id);
	timeout_handler_id = gtk_timeout_add (props->update_interval * 1000, 
					      (GtkFunction)power_applet_timeout_handler, 
					      applet);
	gtk_object_set_data (GTK_OBJECT (applet), 
			     "timeout_handler_id", 
			     GINT_TO_POINTER (timeout_handler_id));

}

static void
power_applet_set_flash_red (GtkWidget *applet, gboolean flash) {
	Properties *props = gtk_object_get_data (GTK_OBJECT (applet), "properties");

	props->flash_red_when_low = flash;
}

static void
power_applet_set_text_aa (GtkWidget *applet, gboolean text_aa) {
	Properties *props = gtk_object_get_data (GTK_OBJECT (applet), "properties");
	GtkWidget *pct_label  = GTK_WIDGET (gtk_object_get_data (GTK_OBJECT (applet), "percent-label"));

	props->text_aa = text_aa;

	if (power_applet_do_label (applet)) {
		gtk_widget_hide_all (pct_label);
	}

	/* reeducate label */
	eel_label_set_smooth_font_size (EEL_LABEL (pct_label), props->smooth_font_size);
	eel_label_set_is_smooth (EEL_LABEL (pct_label), props->text_aa);

	if (power_applet_do_label (applet)) {
		gtk_widget_show_all (pct_label);
	}
}

static void
power_applet_set_label (GtkWidget *applet, gboolean pct, gboolean time, gboolean ac) {
	Properties *props = gtk_object_get_data (GTK_OBJECT (applet), "properties");
	GtkWidget *pct_label  = GTK_WIDGET (gtk_object_get_data (GTK_OBJECT (applet), "percent-label"));

	props->show_percent = pct;
	props->show_time = time;
	props->show_ac = ac;

	if (power_applet_do_label (applet)) {
		/* reeducate label */
		eel_label_set_smooth_font_size (EEL_LABEL (pct_label), props->smooth_font_size);
		eel_label_set_is_smooth (EEL_LABEL (pct_label), props->text_aa);
		gtk_widget_show_all (pct_label);
	} else {
		//props->smooth_font_size = eel_label_get_smooth_font_size (EEL_LABEL (pct_label));
		gtk_widget_hide_all (pct_label);
	}
	power_applet_panel_do_draw_signal (applet, NULL);
}

static void
power_applet_set_show_low_power_dialog (GtkWidget *applet, gboolean show) {
	Properties *props = gtk_object_get_data (GTK_OBJECT (applet), "properties");
	props->show_low_dialog = show;
}

static void
power_applet_set_low_power_threshold (GtkWidget *applet, int low_threshold) {
	Properties *props = gtk_object_get_data (GTK_OBJECT (applet), "properties");
	props->low_threshold = low_threshold;

	g_message ("power_applet_set_low_power_threshold flashing = %s, percent = %d/%d",
		   props->flashing ? "TRUE" : "FALSE", props->info.percent, low_threshold);
	
	if (props->flashing && props->info.percent > low_threshold) {
		power_applet_reset_label (applet);
		power_applet_stop_animation (applet);
	}
}

static void
power_applet_set_show_full_power_dialog (GtkWidget *applet, gboolean show) {
	Properties *props = gtk_object_get_data (GTK_OBJECT (applet), "properties");
	props->show_full_dialog = show;
}

/* check stats, modify the state attribute and return TRUE 
   if redraw is needed */
static void
power_applet_read_device_state (GtkWidget *applet)
{
	Properties *properties;

	properties = gtk_object_get_data (GTK_OBJECT (applet), "properties");
	power_management_read_info (&properties->info);
	power_applet_update_state (applet);
}

static int
power_applet_timeout_handler (GtkWidget *applet)
{
	Properties *properties;
	properties = gtk_object_get_data (GTK_OBJECT (applet), "properties");

	/* g_message ("power_applet_timeout_handler"); */

	if (power_management_present () == PowerManagement_NONE) {
		return FALSE;
	} else {	       
		power_applet_read_device_state (applet);
		return TRUE;
	}
}


static void 
show_error_dialog (gchar *mesg,...) 
{
	GtkWidget *dialogWindow;
	char *tmp;
	va_list ap;

	va_start (ap,mesg);
	tmp = g_strdup_vprintf (mesg,ap);
	dialogWindow = gnome_message_box_new (tmp,GNOME_MESSAGE_BOX_ERROR,
					     GNOME_STOCK_BUTTON_OK,NULL);
	gnome_dialog_run_and_close (GNOME_DIALOG (dialogWindow));
	g_free (tmp);
	va_end (ap);
}

static void 
show_warning_dialog (gchar *mesg,...) 
{
	GtkWidget *dialogWindow;
	char *tmp;
	va_list ap;

	va_start (ap,mesg);
	tmp = g_strdup_vprintf (mesg,ap);
	dialogWindow = gnome_message_box_new (tmp,GNOME_MESSAGE_BOX_WARNING,
					     GNOME_STOCK_BUTTON_OK,NULL);
	gnome_dialog_run_and_close (GNOME_DIALOG (dialogWindow));
	g_free (tmp);
	va_end (ap);
}


static void 
show_message_dialog (char *mesg,...)
{
	GtkWidget *dialogWindow;
	char *tmp;
	va_list ap;

	va_start (ap,mesg);
	tmp = g_strdup_vprintf (mesg,ap);
	dialogWindow = gnome_message_box_new (tmp,GNOME_MESSAGE_BOX_GENERIC,
					     GNOME_STOCK_BUTTON_OK,NULL);
	gnome_dialog_run_and_close (GNOME_DIALOG (dialogWindow));
	g_free (tmp);
	va_end (ap);
}

static void
check_proc_file (GtkWidget *applet)
{
	Properties *properties;
	properties = gtk_object_get_data (GTK_OBJECT (applet), "properties");

	switch (power_management_present ()) {
	case PowerManagement_NONE:
		g_message ("No power management\n");
		show_error_dialog (_("No power management was found"));
		exit (1);
		break;
	case PowerManagement_APM:
		g_message ("APM power management\n");
		power_applet_read_device_state (applet);
		break;
	case PowerManagement_ACPI:
		g_message ("ACPI power management\n");
		power_applet_read_device_state (applet);
		break;
	}	
}

static void
power_applet_load_properties (GtkWidget *applet)
{
	Properties *properties;
	properties = gtk_object_get_data (GTK_OBJECT (applet), "properties");

	g_message ("loading properties from %s", APPLET_WIDGET (applet)->privcfgpath);

	gnome_config_push_prefix (APPLET_WIDGET (applet)->privcfgpath);
	properties->show_percent = gnome_config_get_bool ("power/show_percent=" CFG_SHOW_PERCENT);
	properties->show_time = gnome_config_get_bool ("power/show_time=" CFG_SHOW_TIME);
	properties->show_ac = gnome_config_get_bool ("power/show_ac=" CFG_SHOW_AC);
	properties->show_full_dialog = gnome_config_get_bool ("power/show_full_dialog=" CFG_SHOW_FULL_DIALOG);
	properties->show_low_dialog = gnome_config_get_bool ("power/show_low_dialog=" CFG_SHOW_LOW_DIALOG);
	properties->low_threshold = gnome_config_get_int ("power/low_threshold=" CFG_LOW_THRESHOLD);
	properties->low_threshold = gnome_config_get_int ("power/low_threshold=" CFG_SHUTDOWN_THRESHOLD);
	properties->update_interval = gnome_config_get_int ("power/update_interval=" CFG_UPDATE_INTERVAL);
	properties->text_aa = gnome_config_get_bool ("power/text_aa=" CFG_TEXT_AA);
	properties->flash_red_when_low = gnome_config_get_bool ("power/flash_red_when_low=" CFG_FLASH_TEXT_RED);
	properties->theme = gnome_config_get_string ("power/theme=" CFG_THEME);
	
	properties->message_already_showed = TRUE;

	properties->text_smaller = gnome_config_get_int ("power/text_smaller=5");
	gnome_config_pop_prefix ();
}

static void
power_applet_save_properties (GtkWidget *applet, 
			      const char *cfgpath, 
			      const char *globalcfgpath) 
{
	Properties *properties;
	properties = gtk_object_get_data (GTK_OBJECT (applet), "properties");
	g_assert (properties);

	g_message ("saving session to %s %s", cfgpath, globalcfgpath);

	gnome_config_push_prefix (cfgpath);
	
	gnome_config_set_bool ("power/show_percent", properties->show_percent);
	gnome_config_set_bool ("power/show_time", properties->show_time);
	gnome_config_set_bool ("power/show_ac", properties->show_ac);
	gnome_config_set_bool ("power/show_full_dialog", properties->show_full_dialog);
	gnome_config_set_bool ("power/show_low_dialog", properties->show_low_dialog);
	gnome_config_set_int ("power/low_threshold", properties->low_threshold);
	gnome_config_set_int ("power/update_interval", properties->update_interval);
	gnome_config_set_bool ("power/text_aa", properties->text_aa);
	gnome_config_set_bool ("power/flash_red_when_low", properties->flash_red_when_low);
	gnome_config_set_string ("power/theme", properties->theme);
	
	gnome_config_set_int ("power/text_smaller", properties->text_smaller);
	
	gnome_config_pop_prefix ();
	gnome_config_sync ();
	gnome_config_drop_all ();
}

static void
power_applet_apply_properties_cb (GtkWidget *pb, gpointer unused)
{
	GtkWidget *entry, *entry1, *entry2;
	GtkWidget *menu;
	char *str;

	/* Get all the properties and update the applet */
	entry = gtk_object_get_data (GTK_OBJECT (pb), "show-percent-button");
	entry1 = gtk_object_get_data (GTK_OBJECT (pb), "show-time-button");
	entry2 = gtk_object_get_data (GTK_OBJECT (pb), "show-ac-button");
	power_applet_set_label (global_applet, 
				gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (entry)),
				gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (entry1)),
				gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (entry2)));

	entry = gtk_object_get_data (GTK_OBJECT (pb), "show-low-power-dialog-button");
	power_applet_set_show_low_power_dialog (global_applet, 
						gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (entry)));
	
	entry = gtk_object_get_data (GTK_OBJECT (pb), "show-full-power-dialog-button");
	power_applet_set_show_full_power_dialog (global_applet, 
						 gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (entry)));

	entry = gtk_object_get_data (GTK_OBJECT (pb), "low-power-spin");
	power_applet_set_low_power_threshold (global_applet, 
					      gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (entry)));

	entry = gtk_object_get_data (GTK_OBJECT (pb), "update-interval-spin");
	power_applet_set_update_interval (global_applet, 
					    gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (entry)));

	entry = gtk_object_get_data (GTK_OBJECT (pb), "text-aa");
	power_applet_set_text_aa (global_applet, 
				    gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (entry)));

	entry = gtk_object_get_data (GTK_OBJECT (pb), "flash-red");
	power_applet_set_flash_red (global_applet, 
				    gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (entry)));

	entry = gtk_object_get_data (GTK_OBJECT (pb), "theme-menu");
	menu = gtk_menu_get_active (GTK_MENU (gtk_option_menu_get_menu (GTK_OPTION_MENU (entry))));
	str = gtk_object_get_data (GTK_OBJECT (menu), "theme-selected");
	power_applet_set_theme (global_applet, str);

	/* Save the properties */
	power_applet_save_properties (global_applet, 
					APPLET_WIDGET (global_applet)->privcfgpath,
					APPLET_WIDGET (global_applet)->globcfgpath);
}

static void
power_applet_option_change (GtkWidget *widget, gpointer user_data) {
	GnomePropertyBox *box = GNOME_PROPERTY_BOX (user_data);
	gnome_property_box_changed (box);
}

static void
power_applet_add_theme_menu (GtkWidget *applet,
			     GtkWidget *property_box,
			     GtkWidget *menu,
			     const char *entry)
{
	GtkWidget *item;
	item = gtk_menu_item_new_with_label (entry);
	gtk_object_set_data_full (GTK_OBJECT (item), 
				  "theme-selected",
				  g_strdup (entry),
				  g_free);
	gtk_signal_connect (GTK_OBJECT (item),
			    "activate",
			    GTK_SIGNAL_FUNC (power_applet_option_change),
			    property_box);
	gtk_menu_append (GTK_MENU (menu), item);
}

static void 
power_applet_properties_dialog (GtkWidget *applet, 
				  gpointer data)
{
	GtkWidget *pb;
	GtkWidget *theme, *show_pct, *show_time, *show_ac, 
		*l_dialog, *f_dialog, *low, *interval, *text_aa, *flash_red;
	/* GtkWidget , *text_interval; */
	Properties *properties;
	static GnomeHelpMenuEntry help_entry = {"power-applet","properties"};

	properties = gtk_object_get_data (GTK_OBJECT (applet), "properties");

	if (global_property_box == NULL) {
		xml = glade_xml_new (glade_file,"propertybox1");
		glade_property_box = glade_xml_get_widget (xml,"propertybox1");		
	}

	pb = glade_property_box;

	gtk_window_set_title (GTK_WINDOW (pb), _("PowerApplet properties"));
	theme = glade_xml_get_widget (xml, "theme_menu");
	show_pct = glade_xml_get_widget (xml, "pct_check_button");
	show_time = glade_xml_get_widget (xml, "time_check_button");
	show_ac = glade_xml_get_widget (xml, "ac_check_button");
	l_dialog = glade_xml_get_widget (xml, "low_charge_check_button");
	f_dialog = glade_xml_get_widget (xml, "full_charge_check_button");
	low = glade_xml_get_widget (xml, "low_power_spin");
	interval = glade_xml_get_widget (xml, "interval_spin");
	text_aa = glade_xml_get_widget (xml, "text_aa");
	flash_red = glade_xml_get_widget (xml, "flash_red_check_button");

	/* Set the show-percent thingy */
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (show_pct),
				      properties->show_percent);
	gtk_signal_connect (GTK_OBJECT (show_pct),
			    "toggled",
			    GTK_SIGNAL_FUNC (power_applet_option_change),
			    pb);
	gtk_object_set_data (GTK_OBJECT (pb), "show-percent-button", show_pct);

	/* Set the show-time thingy */
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (show_time),
				      properties->show_time);
	gtk_signal_connect (GTK_OBJECT (show_time),
			    "toggled",
			    GTK_SIGNAL_FUNC (power_applet_option_change),
			    pb);
	gtk_object_set_data (GTK_OBJECT (pb), "show-time-button", show_time);

	/* Set the show-ac thingy */
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (show_ac),
				      properties->show_ac);
	gtk_signal_connect (GTK_OBJECT (show_ac),
			    "toggled",
			    GTK_SIGNAL_FUNC (power_applet_option_change),
			    pb);
	gtk_object_set_data (GTK_OBJECT (pb), "show-ac-button", show_ac);


	/* Set the show-full-power-dialog thingy */
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (f_dialog),
				      properties->show_full_dialog);
	gtk_signal_connect (GTK_OBJECT (f_dialog),
			    "toggled",
			    GTK_SIGNAL_FUNC (power_applet_option_change),
			    pb);
	gtk_object_set_data (GTK_OBJECT (pb), "show-full-power-dialog-button", f_dialog);

	/* Set the show-low-power-dialog thingy */
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (l_dialog),
				      properties->show_low_dialog);
	gtk_signal_connect (GTK_OBJECT (l_dialog),
			    "toggled",
			    GTK_SIGNAL_FUNC (power_applet_option_change),
			    pb);
	gtk_object_set_data (GTK_OBJECT (pb), "show-low-power-dialog-button", l_dialog);

	/* Set the update interval thingy */
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (interval), properties->update_interval);
	gtk_signal_connect (GTK_OBJECT (interval),
			    "changed",
			    GTK_SIGNAL_FUNC (power_applet_option_change),
			    pb);
	gtk_object_set_data (GTK_OBJECT (pb), "update-interval-spin", interval);

	/* Set the low power threshold interval thingy */
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (low), properties->low_threshold);
	gtk_signal_connect (GTK_OBJECT (low),
			    "changed",
			    GTK_SIGNAL_FUNC (power_applet_option_change),
			    pb);
	gtk_object_set_data (GTK_OBJECT (pb), "low-power-spin", low);


	/* Set the use aa text thingy */
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (text_aa),
				      properties->text_aa);
	gtk_signal_connect (GTK_OBJECT (text_aa),
			    "toggled",
			    GTK_SIGNAL_FUNC (power_applet_option_change),
			    pb);
	gtk_object_set_data (GTK_OBJECT (pb), "text-aa", text_aa);

	/* Set the flash-red thingy */
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (flash_red),
				      properties->flash_red_when_low);
	gtk_signal_connect (GTK_OBJECT (flash_red),
			    "toggled",
			    GTK_SIGNAL_FUNC (power_applet_option_change),
			    pb);
	gtk_object_set_data (GTK_OBJECT (pb), "flash-red", flash_red);

	/* Set the theme menu */
	gtk_option_menu_remove_menu (GTK_OPTION_MENU (theme));
	{
		char *pixmapdir;
		DIR *dir;
		struct dirent *dirent;
		GtkWidget *menu;
		int idx = 0, choice = 0;

		pixmapdir = gnome_unconditional_pixmap_file (PACKAGE);
		menu = gtk_menu_new ();

		dir = opendir (pixmapdir);
		/* scan the directory */
		if (!dir) {
			power_applet_add_theme_menu (applet, pb, menu, "no themes");
		} else 	     
			while ((dirent = readdir (dir)) != NULL) {
				/* everything not a ./.. (I assume there's only themes dirs there 
				   gets added to the menu list */
				if (*dirent->d_name != '.') {
					power_applet_add_theme_menu (applet, pb, menu,
								     (const char*)dirent->d_name);
					if (strcmp (properties->theme, dirent->d_name)==0) {
						choice = idx;
					}
					idx++;
				}
			}
		closedir (dir);
		gtk_option_menu_set_menu (GTK_OPTION_MENU (theme), menu);
		gtk_option_menu_set_history (GTK_OPTION_MENU (theme), choice);
	}
	gtk_object_set_data (GTK_OBJECT (pb), "theme-menu", theme);

	gtk_signal_connect (GTK_OBJECT (pb), 
			   "apply", 
			   GTK_SIGNAL_FUNC (power_applet_apply_properties_cb),
			   NULL);
	gtk_signal_connect (GTK_OBJECT (pb),
			   "destroy",
			   GTK_SIGNAL_FUNC (gtk_widget_destroy),
			   pb);
	gtk_signal_connect (GTK_OBJECT (pb),
			   "help",
			   GTK_SIGNAL_FUNC (gnome_help_pbox_display),
			   &help_entry);

	gtk_widget_show_all (pb);
}

static gint 
power_applet_clicked_cb (GtkWidget *applet, 
			   GdkEventButton *e, 
			   gpointer data)
{
	return TRUE; 
}

static void
power_applet_about_cb (AppletWidget *widget, gpointer data)
{
	GtkWidget *about;
	const gchar *authors[] = {"Eskil Heyn Olsen <eskil@eskil.org>",NULL};
	gchar version[] = VERSION;

	about = gnome_about_new (_("Power Applet"), 
				version,
				_("(C) 2001 Free Software Foundation "),
				(const gchar**)authors,
				_("Yet another applet that shows the\n"
				  "waterlevel in Sortedamssøen."),
				NULL);
	gtk_widget_show (about);

	return;
}

static gint
power_applet_save_session (GtkWidget *applet,
			   const char *cfgpath,
			   const char *globcfgpath,
			   gpointer data)
{
	power_applet_save_properties (applet, cfgpath, globcfgpath);
	return FALSE;
}

static void
power_applet_destroy (GtkWidget *applet,gpointer horse)
{
}

static GtkWidget *
power_applet_new (GtkWidget *applet)
{
	GtkWidget *event_box;
	GtkWidget *box;

	/* This is all the data that we associate with the applet instance */
	Properties *properties;
	GtkWidget *pixmap; 
	GtkWidget *pct_label;
	guint timeout_handler_id;

	properties = g_new0 (Properties, 1);
	gtk_object_set_data (GTK_OBJECT (applet), "properties", properties);

	/* this ensures that properties are loaded and 'properties' points to them */
	power_applet_load_properties (applet);
	power_applet_load_theme (applet);

	event_box = gtk_event_box_new ();

	/* construct pixmap widget */
	pixmap = eel_image_new (properties->pixmaps[201]);
	gtk_object_set_data (GTK_OBJECT (applet), "pixmap", pixmap);
	gtk_widget_show_all (pixmap);

	/* construct pct widget */
	pct_label = eel_label_new (NULL);
	eel_label_set_never_smooth (EEL_LABEL (pct_label), FALSE);
	eel_label_set_is_smooth (EEL_LABEL (pct_label), properties->text_aa);
	eel_label_make_smaller (EEL_LABEL (pct_label), properties->text_smaller);
	gtk_object_set_data (GTK_OBJECT (applet), "percent-label", pct_label);
	properties->smooth_font_size = eel_label_get_smooth_font_size (EEL_LABEL (pct_label));

	if (power_applet_do_label (applet)) {
		gtk_widget_show_all (pct_label);
	} else {
		gtk_widget_hide_all (pct_label);
	}
	
	/* pack */
	box = gtk_hbox_new (FALSE, 0);
	gtk_box_pack_start (GTK_BOX (box), pixmap, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (box), pct_label, FALSE, FALSE, 0);
	/* note, I don't use show_all, because this way the percent label is only
	   realised if it's enabled */
	gtk_widget_show (box);

	gtk_widget_set_events (event_box, gtk_widget_get_events (applet) |
			      GDK_BUTTON_PRESS_MASK);

	gtk_signal_connect (GTK_OBJECT (event_box), "button-press-event",
			   GTK_SIGNAL_FUNC (power_applet_clicked_cb), NULL);


	gtk_container_add (GTK_CONTAINER (event_box), box);
  
	gtk_signal_connect (GTK_OBJECT (applet),"save_session",
			   GTK_SIGNAL_FUNC (power_applet_save_session),
			   NULL);
	gtk_signal_connect (GTK_OBJECT (applet),"destroy",
			   GTK_SIGNAL_FUNC (power_applet_destroy),NULL);

	gtk_signal_connect (GTK_OBJECT (applet), "do_draw", 
			    GTK_SIGNAL_FUNC (power_applet_panel_do_draw_signal), 
			    NULL);
	applet_widget_send_draw(APPLET_WIDGET(applet), TRUE);
	power_applet_panel_do_draw_signal (applet, NULL);

	applet_widget_register_stock_callback (APPLET_WIDGET (applet),
					      "properties",
					      GNOME_STOCK_MENU_PROP,
					      _("Properties..."),
					      GTK_SIGNAL_FUNC (power_applet_properties_dialog),
					      NULL);	
	applet_widget_register_stock_callback (APPLET_WIDGET (applet),
					      "about",
					      GNOME_STOCK_MENU_ABOUT,
					      _("About..."),
					      GTK_SIGNAL_FUNC (power_applet_about_cb),
					      NULL);	

	timeout_handler_id = gtk_timeout_add (properties->update_interval * 1000, 
					      (GtkFunction)power_applet_timeout_handler, 
					      applet);

	gtk_object_set_data (GTK_OBJECT (applet), 
			     "timeout_handler_id", 
			     GINT_TO_POINTER (timeout_handler_id));
      
	return event_box;
}

static GtkWidget * applet_start_new_applet (const gchar *goad_id, const char **params, int nparams) {
	GtkWidget *power_applet;
  
	if (strcmp (goad_id, "power-applet")) {
		show_warning_dialog ("goad_id = %s", goad_id);
		return NULL;
	}
     
	global_applet = applet_widget_new (goad_id);
	if (!global_applet) {
		g_error (_("Can't create applet!\n"));
	}
  
	gtk_widget_realize (global_applet);
	power_applet = power_applet_new (global_applet);
	check_proc_file (global_applet);
	gtk_widget_show (power_applet);

	applet_widget_add (APPLET_WIDGET (global_applet), power_applet);
	gtk_widget_show (global_applet);

	return global_applet;
}

static CORBA_Object
applet_activator (CORBA_Object poa,
		  const char *goad_id,
		  const char **params,
		  gpointer *impl_ptr,
		  CORBA_Environment *ev)
{
	GtkWidget *pilot;

	global_applet = applet_widget_new (goad_id);
	if (!global_applet) {
		g_error (_("Can't create applet!\n"));
	}

	gtk_widget_realize (global_applet);
	pilot = power_applet_new (global_applet);
	check_proc_file (global_applet);
	gtk_widget_show (pilot);

  
	applet_widget_add (APPLET_WIDGET (global_applet), pilot);
	gtk_widget_show (global_applet);

  	power_applet_timeout_handler (GTK_WIDGET (global_applet));

	/* make the applet draws initial state */
	power_applet_draw (global_applet);  

	return applet_widget_corba_activate (global_applet, 
					     (PortableServer_POA)poa, 
					     goad_id, 
					     params,
					     impl_ptr, 
					     ev);
}

static void
applet_deactivator (CORBA_Object poa,
		    const char *goad_id,
		    gpointer impl_ptr,
		    CORBA_Environment *ev)
{
	applet_widget_corba_deactivate ((PortableServer_POA)poa, 
					goad_id, 
					impl_ptr, 
					ev);
}

#if defined (SHLIB_APPLETS)
static const char *repo_id[]={"IDL:GNOME/Applet:1.0", NULL};
static GnomePluginObject applets_list[] = {
	{repo_id, "power-applet", NULL, "power applet",
	 &power_applet_activator, &power_applet_deactivator},
	{NULL}
};

GnomePlugin GNOME_Plugin_info = {
	applets_list, NULL
};
#else
int
main (int argc, char *argv[])
{
	gpointer power_applet_impl;

	/* initialize the i18n stuff */
	bindtextdomain (PACKAGE, GNOMELOCALEDIR);
	textdomain (PACKAGE);

	/* initialize applet stuff */
	gnomelib_init ("power-applet", VERSION);
	applet_widget_init ("power-applet", VERSION, 
			    argc, argv, NULL, 0,NULL);

	glade_gnome_init ();
	glade_file = gnome_unconditional_datadir_file (PACKAGE"/power-applet.glade");
	if (!g_file_test (glade_file, G_FILE_TEST_ISFILE)) {
		glade_file = "./power-applet.glade";
		if (!g_file_test (glade_file, G_FILE_TEST_ISFILE)) {
			return -1;
		}
	}

	gnome_window_icon_set_default_from_file (GNOME_ICONDIR"/power-applet.png");

	applet_factory_new ("power-applet_factory", NULL, applet_start_new_applet);

	APPLET_ACTIVATE (applet_activator, "power-applet", &power_applet_impl);

	applet_widget_gtk_main ();

	APPLET_DEACTIVATE (applet_deactivator, "power-applet", power_applet_impl);

	return 0;
}
#endif

