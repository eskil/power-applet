/* 
 * Copyright (C) 2002 Free Software Foundation
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
 * Authors : Eskil Heyn Olsen <eskil@eskil.org>
 */

#include "powerm.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#define PROC_APM "/proc/apm"
#define PROC_ACPI "/proc/acpi"

static PowerManagementType pm_type = PowerManagement_NONE;
static int pm_thread_running = 0;
static int pm_present_called = 0;

/*************************************************************************************/

static int
checkUnit (char **old_unit, char *new_unit) {
	if (new_unit == NULL) return 0;

	if (*old_unit) {
		if (new_unit) {
			/* is it the same ? */
			if ((*old_unit)[0] == new_unit[0] && (*old_unit)[1] == new_unit[1]) {
				/* bah! */
				return -1;
			}
		} else {			
			/* first time we read a unit, get it */
			(*old_unit) = strdup (new_unit);
			if (strlen (new_unit) >= 3) {
				char third = new_unit[2];
				if (third != '\0') if (third != 'h') return -2;
			}
			return 0;
		}
	}
	return 1;
}

static char* 
isIt (const char *query, char *str) {
	char *ptr = str;

	while (*ptr && isspace (*ptr)) ptr++;
	if (strncmp (ptr, query, strlen (query))==0) {
		ptr += strlen (query) + 1;
		while (*ptr && isspace (*ptr)) ptr++;
		if (*ptr != '\0') return ptr;
	}

	return NULL;
}

static void
power_management_read_acpi_ac_adapter (int *result) {
	int adapter = 1;
	int done = 0;
	
	(*result) = 0;

	/* I'm lame, I just check that one AC adapter is on */
	while (!done) {
		char fname[2048];
		FILE *proc;
		snprintf (fname, 2048, "%s/ac_adapter/%d/status", PROC_ACPI, adapter);
		proc = fopen (fname, "rt");
		if (proc) {
			while (!feof (proc)) {
				char line[2048], *ptr;		

				fgets (line, 2048, proc);
				if ((ptr = isIt ("Status:", line))) {
					if (strncmp (ptr, "on-line", 7)==0) {
						(*result) = 1;
					}
					
				}
			}		
			fclose (proc);
			/* is this adapter on ? then stop */
			if (*result) return;
		} else {
			return;
		}
		adapter++;
	}	
}

static int
power_management_read_acpi_battery (int num, PowerInfo *info) {
	char info_fname[2048];
	char status_fname[2048];
	FILE *proc;
	PowerInfo ninfo;
	int there = 0;
	char *unit = NULL;

	memset (&ninfo, 0, sizeof (PowerInfo));

	snprintf (info_fname, 2048, "%s/battery/%d/info", PROC_ACPI, num);
	snprintf (status_fname, 2048, "%s/battery/%d/status", PROC_ACPI, num);

	ninfo.ac_online = 0;
	ninfo.current_charge = 0;
	ninfo.maximum_charge = 0;
	ninfo.percent = 0;
	ninfo.battery_time_left = 0;
	ninfo.recharge_time_left = 0;

	proc = fopen (info_fname, "rt");
	if (proc) {
		while (!feof (proc)) {
			char line[2048], *ptr;			
			char *unit_ptr = NULL;
				
			fgets (line, 2048, proc);
			if ((ptr = isIt ("Present:", line))) {
				if (strncmp (ptr, "yes", 3) == 0) {
					there = 1;
				}
			}
			if ((ptr = isIt ("Design Capacity:", line))) {
				ninfo.maximum_charge = strtol (ptr, &unit_ptr, 10);
			}

			/* sanity check the unity */
			if (checkUnit (&unit, unit_ptr) < 0) {
				fprintf (stderr, "libpowerm: unit %s and/or %s ? wtf ?!\n", unit, unit_ptr);
				return -1;
			}
		}

		if (there) {
			info->maximum_charge += ninfo.maximum_charge;
		}
		fclose (proc);
	} else {
		/* if /proc/acpi/battery/X/info is missing, stop reading battery info */
		return 0;
	}

	proc = fopen (status_fname, "rt");
	if (proc) {
		int rate = 0;
		while (!feof (proc)) {
			char line[2048], *ptr;			
			char *unit_ptr = NULL;
				
			fgets (line, 2048, proc);
			if ((ptr = isIt ("Remaining Capacity:", line))) {
				ninfo.current_charge = strtol (ptr, &unit_ptr, 10);
			}
			if ((ptr = isIt ("Present Rate:", line))) {
				rate = strtol (ptr, &unit_ptr, 10);
			}

			/* sanity check the unity */
			if (checkUnit (&unit, unit_ptr) < 0) {
				fprintf (stderr, "libpowerm: unit %s and/or %s ? wtf ?!\n", unit, unit_ptr);
				return -1;
			}
		}

		if (there) {
			info->current_charge += ninfo.current_charge;
			info->recharge_time_left += (int)((ninfo.maximum_charge*60)/rate);
			info->battery_time_left += (int)((ninfo.current_charge*60)/rate);
		}
		fclose (proc);
	} else {
		/* if /proc/acpi/battery/X/info is missing, stop reading battery info */
		return 0;
	}
	return 1;
}

static int
power_management_read_acpi (PowerInfo *info) {
	int ret = 0;
	int battery = 1;
	int bat_state = 0;

	power_management_read_acpi_ac_adapter (&(info->ac_online));

	do {
		bat_state = power_management_read_acpi_battery (battery, info);
		battery++;
		if (bat_state < 0) {
			/* h0rked */
			ret = 0;
			bat_state = 0;
		}
	}
	while (bat_state);

	{		
		float pct = (float)(info->maximum_charge/100);
		if (pct == 0.0) {
			ret = -1; 
		} else {
			info->percent = (int)(info->current_charge/pct);
		}
	}

	return ret;
}

static int
power_management_read_apm (PowerInfo *info) {
	FILE *proc;
	int ret = 0;
	
	proc = fopen (PROC_APM, "rt");
	if (proc)  {
		int ac, pct, min;
		int filler3, filler4, filler5;
		char line[256];
		char *zeroxpos;
		
		fgets (line, 256, proc);
		zeroxpos = strstr (line, "0x");
		if (zeroxpos) {
			if (sscanf (zeroxpos, "0x%x 0x%x 0x%x 0x%x %d%% %d min",
				    &filler3, 
				    &ac,
				    &filler4, &filler5,
				    &pct, &min) == 6) {			    	
				info->ac_online = ac;
				/* With APM we don't know the battery's charge, use percent */
				info->maximum_charge = 100;
				info->current_charge = pct;
				info->percent = pct;
				info->battery_time_left = min;
				info->recharge_time_left = -1;
			} else {
				ret = -3;
			}
		} else {
			ret = -2;
		}
		
	} else {
		ret = -1;
	}

	if (proc) {
		fclose (proc);
	}

	return ret;
}

/*************************************************************************************/

PowerManagementType 
power_management_present () {
	struct stat sbuf;

	if (pm_present_called) {
		return pm_type;
	}

	pm_present_called = 1;

	/* Test for presence, type and readability of /proc/foo */

	if (stat (PROC_ACPI, &sbuf) == 0) {
		if (S_ISDIR (sbuf.st_mode) && access (PROC_ACPI, R_OK) == 0) {
			pm_type = PowerManagement_ACPI;
			return pm_type;
		}
	}

	if (stat (PROC_APM, &sbuf) == 0) {
		if (S_ISREG (sbuf.st_mode) && access (PROC_APM, R_OK) == 0) {
			pm_type = PowerManagement_APM;
			return pm_type;
		}
	}

	return pm_type;
}

int 
power_management_start_reading (int interval) {
	return -1;
}

int 
power_management_stop_reading () {
	return -1;
}

int 
power_management_read_info (PowerInfo *info) {
	if (pm_present_called == 0) {
		return -10;
	}

	if (pm_thread_running) {
	} else {

		memset (info, 0, sizeof (PowerInfo));

		switch (pm_type) {
		case PowerManagement_APM:
			return power_management_read_apm (info);
		case PowerManagement_ACPI:
			return power_management_read_acpi (info);
		default:
			return -1;
		}
	}
	return -1;
}

int 
power_management_suspend () {
	return -1;
}

int 
power_management_standby () {
	return -1;
}
