/* melder_writetext.cpp
 *
 * Copyright (C) 2007-2011,2015 Paul Boersma
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

/*
 * pb 2007/06/02 utf8 <-> wcs
 * pb 2007/06/14 separated from melder_strings.c and melder_alloc.c
 * pb 2007/06/16 text encoding preferences
 * pb 2007/08/12 prefs in wchar
 * pb 2007/09/04 Melder_malloc rather than malloc in Melder_wcsToAscii (had caused an error in counting memory leaks)
 * pb 2007/10/06 Melder_peekWcsToCfstring
 * pb 2007/12/09 made MelderFile_writeCharacter compatible with the ISO Latin-1 preference
 * pb 2007/12/09 made MelderFile_readText ignore null bytes
 * pb 2008/11/05 split off from melder_encodings.c
 * pb 2010/03/09 support Unicode values above 0xFFFF
 * pb 2011/04/05 C++
 */

#include "melder.h"
#include "Preferences.h"
#include "UnicodeData.h"
#include "abcio.h"

#if defined (macintosh)
	#include "macport_on.h"
    #if useCarbon
        #include <Carbon/Carbon.h>
    #endif
	#include "macport_off.h"
#endif

static void Melder_fwriteUnicodeAsUtf8 (char32 unicode, FILE *f) {
	if (unicode <= 0x0000007F) {
		#ifdef _WIN32
			if (unicode == '\n') fputc (13, f);
		#endif
		fputc ((int) unicode, f);   // because fputc wants an int instead of an uint8_t (guarded conversion)
	} else if (unicode <= 0x000007FF) {
		fputc (0xC0 | (unicode >> 6), f);
		fputc (0x80 | (unicode & 0x0000003F), f);
	} else if (unicode <= 0x0000FFFF) {
		fputc (0xE0 | (unicode >> 12), f);
		fputc (0x80 | ((unicode >> 6) & 0x0000003F), f);
		fputc (0x80 | (unicode & 0x0000003F), f);
	} else {
		fputc (0xF0 | (unicode >> 18), f);
		fputc (0x80 | ((unicode >> 12) & 0x0000003F), f);
		fputc (0x80 | ((unicode >> 6) & 0x0000003F), f);
		fputc (0x80 | (unicode & 0x0000003F), f);
	}
}

void Melder_fwrite32to8 (const char32 *ptr, int64 n, FILE *f) {
	/*
	 * Precondition:
	 *    the string's encoding is UTF-32.
	 * Failure:
	 *    if the precondition does not hold, we don't crash,
	 *    but the characters that are written may be incorrect.
	 */
	for (int64 i = 0; i < n; i ++) {
		Melder_fwriteUnicodeAsUtf8 (ptr [i], f);
	}
}

void MelderFile_writeText (MelderFile file, const char32 *text, enum kMelder_textOutputEncoding outputEncoding) {
	autofile f = Melder_fopen (file, "wb");
	if (outputEncoding == kMelder_textOutputEncoding_UTF8) {
		Melder_fwrite32to8 (text, str32len (text), f);
	} else if ((outputEncoding == kMelder_textOutputEncoding_ASCII_THEN_UTF16 && Melder_isValidAscii (text)) ||
		(outputEncoding == kMelder_textOutputEncoding_ISO_LATIN1_THEN_UTF16 && Melder_isEncodable (text, kMelder_textOutputEncoding_ISO_LATIN1)))
	{
		#ifdef _WIN32
			#define flockfile(f)  (void) 0
			#define funlockfile(f)  (void) 0
			#define putc_unlocked  putc
		#endif
		flockfile (f);
		size_t n = str32len (text);
		for (size_t i = 0; i < n; i ++) {
			char32 kar = text [i];
			#ifdef _WIN32
				if (kar == '\n') putc_unlocked (13, f);
			#endif
			putc_unlocked (kar, f);
		}
		funlockfile (f);
	} else {
		binputu2 (0xFEFF, f);   // Byte Order Mark
		size_t n = str32len (text);
		for (size_t i = 0; i < n; i ++) {
			char32 kar = text [i];
			#ifdef _WIN32
				if (kar == '\n') binputu2 (13, f);
			#endif
			if (kar <= 0x00FFFF) {
				binputu2 ((char16_t) kar, f);   // guarded conversion down
			} else if (kar <= 0x10FFFF) {
				kar -= 0x010000;
				binputu2 (0xD800 | (uint16_t) (kar >> 10), f);
				binputu2 (0xDC00 | (uint16_t) ((char16) kar & 0x3ff), f);
			} else {
				binputu2 (UNICODE_REPLACEMENT_CHARACTER, f);
			}
		}
	}
	f.close (file);
}

void MelderFile_appendText (MelderFile file, const char32 *text) {
	autofile f1;
	try {
		f1.reset (Melder_fopen (file, "rb"));
	} catch (MelderError) {
		Melder_clearError ();   // it's OK if the file didn't exist yet...
		MelderFile_writeText (file, text, Melder_getOutputEncoding ());   // because then we just "write"
		return;
	}
	/*
	 * The file already exists and is open. Determine its type.
	 */
	int firstByte = fgetc (f1), secondByte = fgetc (f1);
	f1.close (file);
	int type = 0;
	if (firstByte == 0xfe && secondByte == 0xff) {
		type = 1;   // big-endian 16-bit
	} else if (firstByte == 0xff && secondByte == 0xfe) {
		type = 2;   // little-endian 16-bit
	}
	if (type == 0) {
		int outputEncoding = Melder_getOutputEncoding ();
		if (outputEncoding == kMelder_textOutputEncoding_UTF8) {   // TODO: read as file's encoding
			autofile f2 = Melder_fopen (file, "ab");
			Melder_fwrite32to8 (text, str32len (text), f2);
			f2.close (file);
		} else if ((outputEncoding == kMelder_textOutputEncoding_ASCII_THEN_UTF16 && Melder_isEncodable (text, kMelder_textOutputEncoding_ASCII))
		    || (outputEncoding == kMelder_textOutputEncoding_ISO_LATIN1_THEN_UTF16 && Melder_isEncodable (text, kMelder_textOutputEncoding_ISO_LATIN1)))
		{
			/*
			 * Append ASCII or ISOLatin1 text to ASCII or ISOLatin1 file.
			 */
			autofile f2 = Melder_fopen (file, "ab");
			int64 n = str32len (text);
			for (int64 i = 0; i < n; i ++) {
				char32 kar = text [i];
				#ifdef _WIN32
					if (kar == U'\n') fputc (13, f2);
				#endif
				fputc ((char8) kar, f2);
			}
			f2.close (file);
		} else {
			/*
			 * Convert to wide character file.
			 */
			autostring32 oldText = MelderFile_readText (file);
			autofile f2 = Melder_fopen (file, "wb");
			binputu2 (0xfeff, f2);
			int64 n = str32len (oldText.peek());
			for (int64 i = 0; i < n; i ++) {
				char32 kar = oldText [i];
				#ifdef _WIN32
					if (kar == U'\n') binputu2 (13, f2);
				#endif
				if (kar <= 0x00FFFF) {
					binputu2 ((uint16) kar, f2);   // guarded conversion down
				} else if (kar <= 0x10FFFF) {
					kar -= 0x010000;
					binputu2 ((uint16) (0x00D800 | (kar >> 10)), f2);
					binputu2 ((uint16) (0x00DC00 | (kar & 0x0003ff)), f2);
				} else {
					binputu2 (UNICODE_REPLACEMENT_CHARACTER, f2);
				}
			}
			n = str32len (text);
			for (int64 i = 0; i < n; i ++) {
				char32 kar = text [i];
				#ifdef _WIN32
					if (kar == '\n') binputu2 (13, f2);
				#endif
				if (kar <= 0x00FFFF) {
					binputu2 ((uint16) kar, f2);   // guarded conversion down
				} else if (kar <= 0x10FFFF) {
					kar -= 0x010000;
					binputu2 ((uint16) (0x00D800 | (kar >> 10)), f2);
					binputu2 ((uint16) (0x00DC00 | (kar & 0x0003ff)), f2);
				} else {
					binputu2 (UNICODE_REPLACEMENT_CHARACTER, f2);
				}
			}
			f2.close (file);
		}
	} else {
		autofile f2 = Melder_fopen (file, "ab");
		int64 n = str32len (text);
		for (int64 i = 0; i < n; i ++) {
			if (type == 1) {
				char32 kar = text [i];
				#ifdef _WIN32
					if (kar == U'\n') binputu2 (13, f2);
				#endif
				if (kar <= 0x00FFFF) {
					binputu2 ((uint16) kar, f2);   // guarded conversion down
				} else if (kar <= 0x10FFFF) {
					kar -= 0x010000;
					binputu2 ((uint16) (0x00D800 | (kar >> 10)), f2);
					binputu2 ((uint16) (0x00DC00 | (kar & 0x0003ff)), f2);
				} else {
					binputu2 (UNICODE_REPLACEMENT_CHARACTER, f2);
				}
			} else {
				char32 kar = text [i];
				#ifdef _WIN32
					if (kar == U'\n') binputu2LE (13, f2);
				#endif
				if (kar <= 0x00FFFF) {
					binputu2LE ((uint16) kar, f2);   // guarded conversion down
				} else if (kar <= 0x10FFFF) {
					kar -= 0x010000;
					binputu2LE ((uint16) (0x00D800 | (kar >> 10)), f2);
					binputu2LE ((uint16) (0x00DC00 | (kar & 0x0003ff)), f2);
				} else {
					binputu2LE (UNICODE_REPLACEMENT_CHARACTER, f2);
				}
			}
		}
		f2.close (file);
	}
}

static void _MelderFile_write (MelderFile file, const char32 *string) {
	if (string == NULL) return;
	int64 length = str32len (string);
	FILE *f = file -> filePointer;
	if (file -> outputEncoding == kMelder_textOutputEncoding_ASCII || file -> outputEncoding == kMelder_textOutputEncoding_ISO_LATIN1) {
		for (int64 i = 0; i < length; i ++) {
			char kar = (char) (char8) string [i];   // truncate
			if (kar == '\n' && file -> requiresCRLF) putc (13, f);
			putc (kar, f);
		}
	} else if (file -> outputEncoding == kMelder_textOutputEncoding_UTF8) {
		for (int64 i = 0; i < length; i ++) {
			char32 kar = string [i];
			if (kar <= 0x00007F) {
				if (kar == U'\n' && file -> requiresCRLF) putc (13, f);
				putc ((int) kar, f);   // guarded conversion down
			} else if (kar <= 0x0007FF) {
				putc (0xC0 | (kar >> 6), f);
				putc (0x80 | (kar & 0x00003F), f);
			} else if (kar <= 0x00FFFF) {
				putc (0xE0 | (kar >> 12), f);
				putc (0x80 | ((kar >> 6) & 0x00003F), f);
				putc (0x80 | (kar & 0x00003F), f);
			} else {
				putc (0xF0 | (kar >> 18), f);
				putc (0x80 | ((kar >> 12) & 0x00003F), f);
				putc (0x80 | ((kar >> 6) & 0x00003F), f);
				putc (0x80 | (kar & 0x00003F), f);
			}
		}
	} else {
		for (int64 i = 0; i < length; i ++) {
			char32 kar = string [i];
			if (kar == U'\n' && file -> requiresCRLF) binputu2 (13, f);
			if (kar <= 0x00FFFF) {
				binputu2 ((char16_t) kar, f);
			} else if (kar <= 0x10FFFF) {
				kar -= 0x010000;
				binputu2 (0xD800 | (char16_t) (kar >> 10), f);
				binputu2 (0xDC00 | (char16_t) ((char16_t) kar & 0x03ff), f);
			} else {
				binputu2 (UNICODE_REPLACEMENT_CHARACTER, f);
			}
		}
	}
}

void MelderFile_writeCharacter (MelderFile file, char32 kar) {
	FILE *f = file -> filePointer;
	if (file -> outputEncoding == kMelder_textOutputEncoding_ASCII || file -> outputEncoding == kMelder_textOutputEncoding_ISO_LATIN1) {
		if (kar == U'\n' && file -> requiresCRLF) putc (13, f);
		putc ((int) kar, f);
	} else if (file -> outputEncoding == kMelder_textOutputEncoding_UTF8) {
		if (kar <= 0x00007F) {
			if (kar == U'\n' && file -> requiresCRLF) putc (13, f);
			putc ((int) kar, f);   // guarded conversion down
		} else if (kar <= 0x0007FF) {
			putc (0xC0 | (kar >> 6), f);
			putc (0x80 | (kar & 0x00003F), f);
		} else if (kar <= 0x00FFFF) {
			putc (0xE0 | (kar >> 12), f);
			putc (0x80 | ((kar >> 6) & 0x00003F), f);
			putc (0x80 | (kar & 0x00003F), f);
		} else {
			putc (0xF0 | (kar >> 18), f);
			putc (0x80 | ((kar >> 12) & 0x00003F), f);
			putc (0x80 | ((kar >> 6) & 0x00003F), f);
			putc (0x80 | (kar & 0x00003F), f);
		}
	} else {
		if (kar == U'\n' && file -> requiresCRLF) binputu2 (13, f);
		if (kar <= 0x00FFFF) {
			binputu2 ((uint16) kar, f);
		} else if (kar <= 0x10FFFF) {
			kar -= 0x010000;
			binputu2 (0xD800 | (uint16) (kar >> 10), f);
			binputu2 (0xDC00 | (uint16) ((uint16) kar & 0x0003ff), f);
		} else {
			binputu2 (UNICODE_REPLACEMENT_CHARACTER, f);
		}
	}
}

void MelderFile_write (MelderFile file, Melder_1_ARG) {
	if (file -> filePointer == NULL) return;
	_MelderFile_write (file, arg1. _arg);
}
void MelderFile_write (MelderFile file, Melder_2_ARGS) {
	if (file -> filePointer == NULL) return;
	_MelderFile_write (file, arg1. _arg);
	_MelderFile_write (file, arg2. _arg);
}
void MelderFile_write (MelderFile file, Melder_3_ARGS) {
	if (file -> filePointer == NULL) return;
	_MelderFile_write (file, arg1. _arg);
	_MelderFile_write (file, arg2. _arg);
	_MelderFile_write (file, arg3. _arg);
}
void MelderFile_write (MelderFile file, Melder_4_ARGS) {
	if (file -> filePointer == NULL) return;
	_MelderFile_write (file, arg1. _arg);
	_MelderFile_write (file, arg2. _arg);
	_MelderFile_write (file, arg3. _arg);
	_MelderFile_write (file, arg4. _arg);
}
void MelderFile_write (MelderFile file, Melder_5_ARGS) {
	if (file -> filePointer == NULL) return;
	_MelderFile_write (file, arg1. _arg);
	_MelderFile_write (file, arg2. _arg);
	_MelderFile_write (file, arg3. _arg);
	_MelderFile_write (file, arg4. _arg);
	_MelderFile_write (file, arg5. _arg);
}
void MelderFile_write (MelderFile file, Melder_6_ARGS) {
	if (file -> filePointer == NULL) return;
	_MelderFile_write (file, arg1. _arg);
	_MelderFile_write (file, arg2. _arg);
	_MelderFile_write (file, arg3. _arg);
	_MelderFile_write (file, arg4. _arg);
	_MelderFile_write (file, arg5. _arg);
	_MelderFile_write (file, arg6. _arg);
}
void MelderFile_write (MelderFile file, Melder_7_ARGS) {
	if (file -> filePointer == NULL) return;
	_MelderFile_write (file, arg1. _arg);
	_MelderFile_write (file, arg2. _arg);
	_MelderFile_write (file, arg3. _arg);
	_MelderFile_write (file, arg4. _arg);
	_MelderFile_write (file, arg5. _arg);
	_MelderFile_write (file, arg6. _arg);
	_MelderFile_write (file, arg7. _arg);
}
void MelderFile_write (MelderFile file, Melder_8_ARGS) {
	if (file -> filePointer == NULL) return;
	_MelderFile_write (file, arg1. _arg);
	_MelderFile_write (file, arg2. _arg);
	_MelderFile_write (file, arg3. _arg);
	_MelderFile_write (file, arg4. _arg);
	_MelderFile_write (file, arg5. _arg);
	_MelderFile_write (file, arg6. _arg);
	_MelderFile_write (file, arg7. _arg);
	_MelderFile_write (file, arg8. _arg);
}
void MelderFile_write (MelderFile file, Melder_9_ARGS) {
	if (file -> filePointer == NULL) return;
	_MelderFile_write (file, arg1. _arg);
	_MelderFile_write (file, arg2. _arg);
	_MelderFile_write (file, arg3. _arg);
	_MelderFile_write (file, arg4. _arg);
	_MelderFile_write (file, arg5. _arg);
	_MelderFile_write (file, arg6. _arg);
	_MelderFile_write (file, arg7. _arg);
	_MelderFile_write (file, arg8. _arg);
	_MelderFile_write (file, arg9. _arg);
}
void MelderFile_write (MelderFile file, Melder_10_ARGS) {
	if (file -> filePointer == NULL) return;
	_MelderFile_write (file, arg1. _arg);
	_MelderFile_write (file, arg2. _arg);
	_MelderFile_write (file, arg3. _arg);
	_MelderFile_write (file, arg4. _arg);
	_MelderFile_write (file, arg5. _arg);
	_MelderFile_write (file, arg6. _arg);
	_MelderFile_write (file, arg7. _arg);
	_MelderFile_write (file, arg8. _arg);
	_MelderFile_write (file, arg9. _arg);
	_MelderFile_write (file, arg10._arg);
}
void MelderFile_write (MelderFile file, Melder_11_ARGS) {
	if (file -> filePointer == NULL) return;
	_MelderFile_write (file, arg1. _arg);
	_MelderFile_write (file, arg2. _arg);
	_MelderFile_write (file, arg3. _arg);
	_MelderFile_write (file, arg4. _arg);
	_MelderFile_write (file, arg5. _arg);
	_MelderFile_write (file, arg6. _arg);
	_MelderFile_write (file, arg7. _arg);
	_MelderFile_write (file, arg8. _arg);
	_MelderFile_write (file, arg9. _arg);
	_MelderFile_write (file, arg10._arg);
	_MelderFile_write (file, arg11._arg);
}
void MelderFile_write (MelderFile file, Melder_13_ARGS) {
	if (file -> filePointer == NULL) return;
	_MelderFile_write (file, arg1. _arg);
	_MelderFile_write (file, arg2. _arg);
	_MelderFile_write (file, arg3. _arg);
	_MelderFile_write (file, arg4. _arg);
	_MelderFile_write (file, arg5. _arg);
	_MelderFile_write (file, arg6. _arg);
	_MelderFile_write (file, arg7. _arg);
	_MelderFile_write (file, arg8. _arg);
	_MelderFile_write (file, arg9. _arg);
	_MelderFile_write (file, arg10._arg);
	_MelderFile_write (file, arg11._arg);
	_MelderFile_write (file, arg12._arg);
	_MelderFile_write (file, arg13._arg);
}
void MelderFile_write (MelderFile file, Melder_15_ARGS) {
	if (file -> filePointer == NULL) return;
	_MelderFile_write (file, arg1. _arg);
	_MelderFile_write (file, arg2. _arg);
	_MelderFile_write (file, arg3. _arg);
	_MelderFile_write (file, arg4. _arg);
	_MelderFile_write (file, arg5. _arg);
	_MelderFile_write (file, arg6. _arg);
	_MelderFile_write (file, arg7. _arg);
	_MelderFile_write (file, arg8. _arg);
	_MelderFile_write (file, arg9. _arg);
	_MelderFile_write (file, arg10._arg);
	_MelderFile_write (file, arg11._arg);
	_MelderFile_write (file, arg12._arg);
	_MelderFile_write (file, arg13._arg);
	_MelderFile_write (file, arg14._arg);
	_MelderFile_write (file, arg15._arg);
}
void MelderFile_write (MelderFile file, Melder_19_ARGS) {
	if (file -> filePointer == NULL) return;
	_MelderFile_write (file, arg1. _arg);
	_MelderFile_write (file, arg2. _arg);
	_MelderFile_write (file, arg3. _arg);
	_MelderFile_write (file, arg4. _arg);
	_MelderFile_write (file, arg5. _arg);
	_MelderFile_write (file, arg6. _arg);
	_MelderFile_write (file, arg7. _arg);
	_MelderFile_write (file, arg8. _arg);
	_MelderFile_write (file, arg9. _arg);
	_MelderFile_write (file, arg10._arg);
	_MelderFile_write (file, arg11._arg);
	_MelderFile_write (file, arg12._arg);
	_MelderFile_write (file, arg13._arg);
	_MelderFile_write (file, arg14._arg);
	_MelderFile_write (file, arg15._arg);
	_MelderFile_write (file, arg16._arg);
	_MelderFile_write (file, arg17._arg);
	_MelderFile_write (file, arg18._arg);
	_MelderFile_write (file, arg19._arg);
}

/* End of file melder_writetext.cpp */
