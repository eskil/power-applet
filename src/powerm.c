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

#define PROC_APM "/proc/apm"
#define PROC_ACPI "/proc/acpi"

static PowerManagementType pm_type = PowerManagement_NONE;
static int pm_thread_running = 0;
static int pm_present_called = 0;

/*************************************************************************************/

static int
power_management_read_acpi (PowerInfo *info) {
	return -42;
}

static int
power_management_read_apm (PowerInfo *info) {
	FILE *proc;
	int ret = 0;
	
	proc = fopen ("/proc/apm", "rt");
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
	fclose (proc);

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

	if (stat (PROC_APM, &sbuf) == 0) {
		if (S_ISREG (sbuf.st_mode) && access (PROC_APM, R_OK) == 0) {
			pm_type = PowerManagement_APM;
		}
	}

	if (stat (PROC_ACPI, &sbuf) == 0) {
		if (S_ISDIR (sbuf.st_mode) && access (PROC_ACPI, R_OK) == 0) {
			pm_type = PowerManagement_ACPI;
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
