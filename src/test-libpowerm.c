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

#include <unistd.h>
#include <stdio.h>

int
main (int argc, char *argv[]) {
	switch (power_management_present ()) {
	case PowerManagement_NONE:
		printf ("No power management\n");
		exit (1);
		break;
	case PowerManagement_APM:
		printf ("APM power management\n");
		break;
	case PowerManagement_ACPI:
		printf ("ACPI power management\n");
		break;
	}
	
	while (1) {
		PowerInfo info;
		int err;
		err = power_management_read_info (&info);
		if (err == 0) {
			if (info.ac_online) {
				printf ("Recharging, charge is %d/%d (%d%%), %d min left to full charge (%d min)\n",
					info.current_charge, info.maximum_charge,
					info.percent,
					info.recharge_time_left,
					info.battery_time_left);
			} else {
				printf ("Discharging, charge is %d/%d (%d%%), %d min left\n",
					info.current_charge, info.maximum_charge,
					info.percent, info.battery_time_left);
			}
		} else {
			printf ("power_management_read_info returned %d\n", err);
			return -1;
		}
		sleep (1);
	}

	return 0;
}

