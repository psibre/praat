/* prefs_copyToInstance.h
 *
 * Copyright (C) 2013,2015 Paul Boersma
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* for C++ files; see prefs.h */

#undef prefs_begin
#undef prefs_add_int
#undef prefs_add_int_with_data
#undef prefs_override_int
#undef prefs_add_long
#undef prefs_add_long_with_data
#undef prefs_override_long
#undef prefs_add_bool
#undef prefs_add_bool_with_data
#undef prefs_override_bool
#undef prefs_add_double
#undef prefs_add_double_with_data
#undef prefs_override_double
#undef prefs_add_enum
#undef prefs_add_enum_with_data
#undef prefs_override_enum
#undef prefs_add_string
#undef prefs_add_string_with_data
#undef prefs_override_string
#undef prefs_end

#define prefs_begin(Klas) \
	void struct##Klas :: v_copyPreferencesToInstance () { \
		Klas##_Parent :: v_copyPreferencesToInstance ();

#define prefs_add_int(Klas,name,version,default)
#define prefs_add_int_with_data(Klas,name,version,default)  p_##name = pref_##name ();
#define prefs_override_int(Klas,name,version,default)

#define prefs_add_long(Klas,name,version,default)
#define prefs_add_long_with_data(Klas,name,version,default)  p_##name = pref_##name ();
#define prefs_override_long(Klas,name,version,default)

#define prefs_add_bool(Klas,name,version,default)
#define prefs_add_bool_with_data(Klas,name,version,default)  p_##name = pref_##name ();
#define prefs_override_bool(Klas,name,version,default)

#define prefs_add_double(Klas,name,version,default)
#define prefs_add_double_with_data(Klas,name,version,default)  p_##name = pref_##name ();
#define prefs_override_double(Klas,name,version,default)

#define prefs_add_enum(Klas,name,version,enumerated,default)
#define prefs_add_enum_with_data(Klas,name,version,enumerated,default)  p_##name = pref_##name ();
#define prefs_override_enum(Klas,name,version,enumerated,default)

#define prefs_add_string(Klas,name,version,default)
#define prefs_add_string_with_data(Klas,name,version,default)  str32cpy (& p_##name [0], pref_##name ());
#define prefs_override_string(Klas,name,version,default)

#define prefs_end(Klas) \
	}

/* End of file prefs_copyToInstance.h */
