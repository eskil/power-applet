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


/*

This is the main header file for libpowerm, yet another library to
access power management (APM and ACPI) info.

*/

#ifndef __powerm_h__
#define __powerm_h__

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef enum {
	PowerManagement_NONE = 0,
	PowerManagement_APM = 1,
	PowerManagement_ACPI = 2
} PowerManagementType;

typedef struct {
	/* 0 for battery, 1 for AC adapter online */
	int ac_online; 

	/* Battery's current and maximun charge */
	int current_charge, maximum_charge;

	/* Battery charge in percent */
	int percent;

	/* Time left without AC, in minutes. Negative value means unknown */
	int battery_time_left;
	
	/* If ac_online == 1, minutes left till battery fully charged. Negative value means unknown */
	int recharge_time_left;
} PowerInfo;

/* 
   Returns a PowerManagementType indicating the available type.
   A PowerManagementType_NONE doesn't mean the machine doesn't 
   have PM, but just that the /proc files aren't present.
   For normal usage, just check that the 
   power_management_present () != PowerManagement_NONE 
*/
PowerManagementType power_management_present (void);

/*
  This function can be called (only once) before using the power_management_read_info.
  It starts a thread that reads the PM files every "interval" second (just call
  with 1). This is needed to compute battery charge/discharge times.
  Returns 0 on success, negative on failure :
  -1 = already called, all should be ok
  -2 = cannot start thread, discharge/recharge times may be horked

  If this is not called, every call to power_mangement_read_info will cause
  a read of the PM files.
 */
int power_management_start_reading (int interval);	

/*
  This function must be called before using the power_management_read_info.
  This terminates the reader thread. You may call this and then restart
  with power_management_start_reading if you eg. need to change the interval.
 */
int power_management_stop_reading (void);	

/*
  Fills *info with the current PM info, returns 1 on success,
  0 on failure.
  Note this returns the most recent read from the reader thread. If the reader thread
  wasn't started, this will do an read of the PM files.
 */
int power_management_read_info (PowerInfo *info);       

/*
  Tries to suspend the machine.
  If PM type is APM, this is done by calling "apm --suspend",
  doesn't do jack for ACPI yet.
 */
int power_management_suspend (void);

/*
  Tries to place the machine in standby.
  If PM type is APM, this is done by calling "apm --standby",
  doesn't do jack for ACPI yet.
 */
int power_management_standby (void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __powerm_h__ */
