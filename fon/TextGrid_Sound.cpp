/* TextGrid_Sound.cpp
 *
 * Copyright (C) 1992-2011,2013,2014,2015 Paul Boersma
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
 * pb 2010/12/06 BDF/EDF files (for EEG)
 * pb 2010/12/08 split off from TextGrid.c and Sound.c
 * pb 2011/06/11 C++
 */

#include "TextGrid_Sound.h"
#include "Pitch_to_PitchTier.h"
#include "SpeechSynthesizer_and_TextGrid.h"
#include "LongSound.h"

static bool IntervalTier_check (IntervalTier me) {
	for (long iinterval = 1; iinterval <= my numberOfIntervals (); iinterval ++) {
		TextInterval interval = my interval (iinterval);
		if (interval -> xmin >= interval -> xmax)
			return false;
	}
	if (my numberOfIntervals () < 2) return true;
	for (long iinterval = 1; iinterval < my numberOfIntervals (); iinterval ++) {
		TextInterval interval = my interval (iinterval), nextInterval = my interval (iinterval + 1);
		if (interval -> xmax != nextInterval -> xmin)
			return false;
	}
	return true;
}

static void IntervalTier_insertIntervalDestructively (IntervalTier me, double tmin, double tmax) {
	Melder_assert (tmin < tmax);
	Melder_assert (tmin >= my xmin);
	Melder_assert (tmax <= my xmax);
	/*
	 * Make sure that the tier has boundaries at the edges of the interval.
	 */
	long firstIntervalNumber = IntervalTier_hasTime (me, tmin);
	if (! firstIntervalNumber) {
		long intervalNumber = IntervalTier_timeToIndex (me, tmin);
		if (intervalNumber == 0)
			Melder_throw (U"Cannot add a boundary at ", Melder_fixed (tmin, 6), U" seconds, because this is outside the time domain of the intervals.");
		TextInterval interval = my interval (intervalNumber);
		/*
		 * Move the text to the left of the boundary.
		 */
		autoTextInterval newInterval = TextInterval_create (tmin, interval -> xmax, U"");
		interval -> xmax = tmin;
		Collection_addItem (my intervals, newInterval.transfer());
		firstIntervalNumber = IntervalTier_hasTime (me, interval -> xmin);
	}
	Melder_assert (firstIntervalNumber >= 1 && firstIntervalNumber <= my intervals -> size);
	long lastIntervalNumber = IntervalTier_hasTime (me, tmax);
	if (! lastIntervalNumber) {
		long intervalNumber = IntervalTier_timeToIndex (me, tmax);
		if (intervalNumber == 0)
			Melder_throw (U"Cannot add a boundary at ", Melder_fixed (tmin, 6), U" seconds, because this is outside the time domain of the intervals.");
		TextInterval interval = my interval (intervalNumber);
		/*
		 * Move the text to the right of the boundary.
		 */
		autoTextInterval newInterval = TextInterval_create (interval -> xmin, tmax, U"");
		interval -> xmin = tmax;
		Collection_addItem (my intervals, newInterval.transfer());
		lastIntervalNumber = IntervalTier_hasTime (me, interval -> xmax);
	}
	Melder_assert (lastIntervalNumber >= 1 && lastIntervalNumber <= my intervals -> size);
	/*
	 * Empty the interval in the word tier.
	 */
	trace (U"Empty interval %ld down to ", lastIntervalNumber, U".", firstIntervalNumber);
	for (long iinterval = lastIntervalNumber; iinterval >= firstIntervalNumber; iinterval --) {
		TextInterval interval = my interval (iinterval);
		if (interval -> xmin > tmin && interval -> xmin < tmax) {
			Melder_assert (iinterval > 1);
			TextInterval previous = (TextInterval) my intervals -> item [iinterval - 1];
			previous -> xmax = tmax;   // collapse left and right intervals into left interval
			TextInterval_setText (previous, U"");
			Collection_removeItem (my intervals, iinterval);   // remove right interval
		}
		if (interval -> xmax == tmax) {
			TextInterval_setText (interval, U"");
		}
	}
}

static double IntervalTier_boundaryTimeClosestTo (IntervalTier me, double tmin, double tmax) {
	long intervalNumber = IntervalTier_timeToLowIndex (me, tmax);
	if (intervalNumber != 0) {
		TextInterval interval = my interval (intervalNumber);
		if (interval -> xmin > tmin && interval -> xmin < tmax) {
			return interval -> xmin;
		}
	}
	return 0.5 * (tmin + tmax);
}

static void IntervalTier_removeEmptyIntervals (IntervalTier me, IntervalTier boss) {
	IntervalTier_removeBoundariesBetweenIdenticallyLabeledIntervals (me, U"");
	if (my intervals -> size < 2) return;
	TextInterval firstInterval = my interval (1);
	if (Melder_equ (firstInterval -> text, U"")) {
		IntervalTier_removeLeftBoundary (me, 2);
	}
	if (my numberOfIntervals () < 2) return;
	TextInterval lastInterval = my interval (my numberOfIntervals ());
	if (Melder_equ (lastInterval -> text, U"")) {
		IntervalTier_removeLeftBoundary (me, my numberOfIntervals ());
	}
	if (my numberOfIntervals () < 3) return;
	for (long iinterval = my numberOfIntervals () - 1; iinterval >= 2; iinterval --) {
		TextInterval interval = my interval (iinterval);
		if (Melder_equ (interval -> text, U"")) {
			/*
			 * Distribute the empty interval between its neigbours.
			 */
			double newBoundaryTime =
				boss ?
				IntervalTier_boundaryTimeClosestTo (boss, interval -> xmin, interval -> xmax) :
				0.5 * (interval -> xmin + interval -> xmax);
			TextInterval previous = my interval (iinterval - 1);
			TextInterval next = my interval (iinterval + 1);
			previous -> xmax = newBoundaryTime;
			next -> xmin = newBoundaryTime;
			Collection_removeItem (my intervals, iinterval);
		}
	}
}

void TextGrid_anySound_alignInterval (TextGrid me, Function anySound, long tierNumber, long intervalNumber, const char32 *languageName, bool includeWords, bool includePhonemes) {
	try {
		IntervalTier headTier = TextGrid_checkSpecifiedTierIsIntervalTier (me, tierNumber);
		if (intervalNumber < 1 || intervalNumber > headTier -> numberOfIntervals ())
			Melder_throw (U"Interval ", intervalNumber, U" does not exist.");
		TextInterval interval = headTier -> interval (intervalNumber);
		if (! includeWords && ! includePhonemes)
			Melder_throw (U"Nothing to be done, because you asked neither for word alignment nor for phoneme alignment.");
		if (str32str (headTier -> name, U"/") )
			Melder_throw (U"The current tier already has a slash (\"/\") in its name. Cannot create a word or phoneme tier from it.");
		autoSound part =
			anySound -> classInfo == classLongSound ? 
				LongSound_extractPart (static_cast <LongSound> (anySound), interval -> xmin, interval -> xmax, true) :
				Sound_extractPart (static_cast <Sound> (anySound), interval -> xmin, interval -> xmax, kSound_windowShape_RECTANGULAR, 1.0, true);
		autoSpeechSynthesizer synthesizer = SpeechSynthesizer_create (languageName, U"default");
		double silenceThreshold = -35, minSilenceDuration = 0.1, minSoundingDuration = 0.1;
		autoTextGrid analysis = NULL;
		if (! Melder_equ (interval -> text, U"")) {
			try {
				analysis.reset (SpeechSynthesizer_and_Sound_and_TextInterval_align
					(synthesizer.peek(), part.peek(), interval, silenceThreshold, minSilenceDuration, minSoundingDuration));
			} catch (MelderError) {
				Melder_clearError ();   // ignore all error messages from DTW and the like
			}
		}
		if (analysis.peek()) {
			/*
			 * Clean up the analysis.
			 */
			Melder_assert (analysis -> xmin == interval -> xmin);
			Melder_assert (analysis -> xmax == interval -> xmax);
			Melder_assert (analysis -> numberOfTiers () == 4);
			Thing_cast (IntervalTier, analysisWordTier, analysis -> tier (3));
			if (! IntervalTier_check (analysisWordTier))
				Melder_throw (U"Analysis word tier out of order.");
			IntervalTier_removeEmptyIntervals (analysisWordTier, NULL);
			Melder_assert (analysisWordTier -> xmax == analysis -> xmax);
			Melder_assert (analysisWordTier -> numberOfIntervals () >= 1);
			TextInterval firstInterval = analysisWordTier -> interval (1);
			TextInterval lastInterval = analysisWordTier -> interval (analysisWordTier -> numberOfIntervals ());
			firstInterval -> xmin = analysis -> xmin;
			lastInterval  -> xmax = analysis -> xmax;
			if (lastInterval -> xmax != analysis -> xmax)
				Melder_fatal (U"analysis ends at ", analysis -> xmax, U", but last interval at ", lastInterval -> xmax, U" seconds");
			if (! IntervalTier_check (analysisWordTier))
				Melder_throw (U"Analysis word tier out of order (2).");
			Thing_cast (IntervalTier, analysisPhonemeTier, analysis -> tier (4));
			if (! IntervalTier_check (analysisPhonemeTier))
				Melder_throw (U"Analysis phoneme tier out of order.");
			IntervalTier_removeEmptyIntervals (analysisPhonemeTier, analysisWordTier);
			Melder_assert (analysisPhonemeTier -> xmax == analysis -> xmax);
			Melder_assert (analysisPhonemeTier -> numberOfIntervals () >= 1);
			firstInterval = analysisPhonemeTier -> interval (1);
			lastInterval  = analysisPhonemeTier -> interval (analysisPhonemeTier -> numberOfIntervals ());
			firstInterval -> xmin = analysis -> xmin;
			lastInterval  -> xmax = analysis -> xmax;
			Melder_assert (lastInterval -> xmax == analysis -> xmax);
			if (! IntervalTier_check (analysisPhonemeTier))
				Melder_throw (U"Analysis phoneme tier out of order (2).");
		}
		long wordTierNumber = 0, phonemeTierNumber = 0;
		IntervalTier wordTier = NULL, phonemeTier = NULL;
		/*
		 * Include a word tier.
		 */
		if (includeWords) {
			/*
			 * Make sure that the word tier exists.
			 */
			autoMelderString newWordTierName;
			MelderString_copy (& newWordTierName, headTier -> name, U"/word");
			for (long itier = 1; itier <= my numberOfTiers (); itier ++) {
				IntervalTier tier = static_cast <IntervalTier> (my tier (itier));
				if (Melder_equ (newWordTierName.string, tier -> name)) {
					if (tier -> classInfo != classIntervalTier)
						Melder_throw (U"A tier with the prospective word tier name (", tier -> name, U") already exists, but it is not an interval tier."
							U"\nPlease change its name or remove it.");
					wordTierNumber = itier;
					break;
				}
			}
			if (! wordTierNumber) {
				autoIntervalTier newWordTier = IntervalTier_create (my xmin, my xmax);
				Thing_setName (newWordTier.peek(), newWordTierName.string);
				Ordered_addItemPos (my tiers, newWordTier.transfer(), wordTierNumber = tierNumber + 1);
			}
			Melder_assert (wordTierNumber >= 1 && wordTierNumber <= my tiers -> size);
			wordTier = static_cast <IntervalTier> (my tier (wordTierNumber));
			/*
			 * Make sure that the word tier has boundaries at the edges of the interval.
			 */
			IntervalTier_insertIntervalDestructively (wordTier, interval -> xmin, interval -> xmax);
			/*
			 * Copy the contents of the word analysis into the interval in the word tier.
			 */
			long wordIntervalNumber = IntervalTier_hasTime (wordTier, interval -> xmin);
			Melder_assert (wordIntervalNumber != 0);
			if (analysis.peek()) {
				Thing_cast (IntervalTier, analysisWordTier, analysis -> tier (3));
				if (! IntervalTier_check (analysisWordTier))
					Melder_throw (U"Analysis word tier out of order (3).");
				if (! IntervalTier_check (wordTier))
					Melder_throw (U"Word tier out of order (3).");
				for (long ianalysisInterval = 1; ianalysisInterval <= analysisWordTier -> numberOfIntervals (); ianalysisInterval ++) {
					TextInterval analysisInterval = analysisWordTier -> interval (ianalysisInterval);
					TextInterval wordInterval = NULL;
					double tmin = analysisInterval -> xmin, tmax = analysisInterval -> xmax;
					if (tmax == analysis -> xmax) {
						wordInterval = wordTier -> interval (wordIntervalNumber);
						TextInterval_setText (wordInterval, analysisInterval -> text);
					} else {
						wordInterval = wordTier -> interval (wordIntervalNumber);
						autoTextInterval newInterval = TextInterval_create (tmin, tmax, analysisInterval -> text);
						wordInterval -> xmin = tmax;
						Collection_addItem (wordTier -> intervals, newInterval.transfer());
						wordIntervalNumber ++;
					}
				}
				if (! IntervalTier_check (analysisWordTier))
					Melder_throw (U"Analysis word tier out of order (4).");
				if (! IntervalTier_check (wordTier))
					Melder_throw (U"Word tier out of order (4).");
			}
		}
		/*
		 * Include a phoneme tier.
		 */
		if (includePhonemes) {
			/*
			 * Make sure that the phoneme tier exists.
			 */
			autoMelderString newPhonemeTierName;
			MelderString_copy (& newPhonemeTierName, headTier -> name, U"/phon");
			for (long itier = 1; itier <= my numberOfTiers (); itier ++) {
				IntervalTier tier = static_cast <IntervalTier> (my tier (itier));
				if (Melder_equ (newPhonemeTierName.string, tier -> name)) {
					if (tier -> classInfo != classIntervalTier)
						Melder_throw (U"A tier with the prospective phoneme tier name (", tier -> name, U") already exists, but it is not an interval tier."
							U"\nPlease change its name or remove it.");
					phonemeTierNumber = itier;
					break;
				}
			}
			if (! phonemeTierNumber) {
				autoIntervalTier newPhonemeTier = IntervalTier_create (my xmin, my xmax);
				Thing_setName (newPhonemeTier.peek(), newPhonemeTierName.string);
				Ordered_addItemPos (my tiers, newPhonemeTier.transfer(), phonemeTierNumber = wordTierNumber ? wordTierNumber + 1 : tierNumber + 1);
			}
			Melder_assert (phonemeTierNumber >= 1 && phonemeTierNumber <= my tiers -> size);
			phonemeTier = static_cast <IntervalTier> (my tiers -> item [phonemeTierNumber]);
			/*
			 * Make sure that the phoneme tier has boundaries at the edges of the interval.
			 */
			IntervalTier_insertIntervalDestructively (phonemeTier, interval -> xmin, interval -> xmax);
			/*
			 * Copy the contents of the phoneme analysis into the interval in the phoneme tier.
			 */
			long phonemeIntervalNumber = IntervalTier_hasTime (phonemeTier, interval -> xmin);
			Melder_assert (phonemeIntervalNumber != 0);
			if (analysis.peek()) {
				Thing_cast (IntervalTier, analysisPhonemeTier, analysis -> tiers -> item [4]);
				for (long ianalysisInterval = 1; ianalysisInterval <= analysisPhonemeTier -> numberOfIntervals (); ianalysisInterval ++) {
					TextInterval analysisInterval = analysisPhonemeTier -> interval (ianalysisInterval);
					TextInterval phonemeInterval = NULL;
					double tmin = analysisInterval -> xmin, tmax = analysisInterval -> xmax;
					if (tmax == analysis -> xmax) {
						phonemeInterval = phonemeTier -> interval (phonemeIntervalNumber);
						TextInterval_setText (phonemeInterval, analysisInterval -> text);
					} else {
						phonemeInterval = phonemeTier -> interval (phonemeIntervalNumber);
						autoTextInterval newInterval = TextInterval_create (tmin, tmax, analysisInterval -> text);
						phonemeInterval -> xmin = tmax;
						Collection_addItem (phonemeTier -> intervals, newInterval.transfer());
						phonemeIntervalNumber ++;
					}
				}
			}
			if (includeWords) {
				/*
				 * Synchronize the boundaries between the word tier and the phoneme tier.
				 */
				//for (long iinterval = 1; iinterval <=
			}
		}
	} catch (MelderError) {
		Melder_throw (me, U" & ", anySound, U": interval not aligned.");
	}
}

void TextGrid_Sound_draw (TextGrid me, Sound sound, Graphics g, double tmin, double tmax,
	bool showBoundaries, bool useTextStyles, bool garnish)   // STEREO BUG
{
	long numberOfTiers = my numberOfTiers ();

	/*
	 * Automatic windowing:
	 */
	if (tmax <= tmin) tmin = my xmin, tmax = my xmax;

	Graphics_setInner (g);
	Graphics_setWindow (g, tmin, tmax, -1.0 - 0.5 * numberOfTiers, 1.0);

	/*
	 * Draw sound in upper part.
	 */
	long first, last;
	if (sound && Sampled_getWindowSamples (sound, tmin, tmax, & first, & last) > 1) {
		Graphics_setLineType (g, Graphics_DOTTED);
		Graphics_line (g, tmin, 0.0, tmax, 0.0);
		Graphics_setLineType (g, Graphics_DRAWN);      
		Graphics_function (g, sound -> z [1], first, last,
			Sampled_indexToX (sound, first), Sampled_indexToX (sound, last));
	}

	/*
	 * Draw labels in lower part.
	 */
	Graphics_setTextAlignment (g, Graphics_CENTRE, Graphics_HALF);
	Graphics_setPercentSignIsItalic (g, useTextStyles);
	Graphics_setNumberSignIsBold (g, useTextStyles);
	Graphics_setCircumflexIsSuperscript (g, useTextStyles);
	Graphics_setUnderscoreIsSubscript (g, useTextStyles);
	for (long itier = 1; itier <= numberOfTiers; itier ++) {
		Function anyTier = my tier (itier);
		double ymin = -1.0 - 0.5 * itier, ymax = ymin + 0.5;
		Graphics_rectangle (g, tmin, tmax, ymin, ymax);
		if (anyTier -> classInfo == classIntervalTier) {
			IntervalTier tier = static_cast <IntervalTier> (anyTier);
			long ninterval = tier -> numberOfIntervals ();
			for (long iinterval = 1; iinterval <= ninterval; iinterval ++) {
				TextInterval interval = tier -> interval (iinterval);
				double intmin = interval -> xmin, intmax = interval -> xmax;
				if (intmin < tmin) intmin = tmin;
				if (intmax > tmax) intmax = tmax;
				if (intmin >= intmax) continue;
				if (showBoundaries && intmin > tmin && intmin < tmax) {
					Graphics_setLineType (g, Graphics_DOTTED);
					Graphics_line (g, intmin, -1.0, intmin, 1.0);   /* In sound part. */
					Graphics_setLineType (g, Graphics_DRAWN);
				}      
				/* Draw left boundary. */
				if (intmin > tmin && intmin < tmax) Graphics_line (g, intmin, ymin, intmin, ymax);
				/* Draw label text. */
				if (interval -> text && intmax >= tmin && intmin <= tmax) {
					double t1 = tmin > intmin ? tmin : intmin;
					double t2 = tmax < intmax ? tmax : intmax;
					Graphics_text (g, 0.5 * (t1 + t2), 0.5 * (ymin + ymax), interval -> text);
				}
			}
		} else {
			TextTier tier = static_cast <TextTier> (anyTier);
			long numberOfPoints = tier -> numberOfPoints ();
			for (long ipoint = 1; ipoint <= numberOfPoints; ipoint ++) {
				TextPoint point = tier -> point (ipoint);
				double t = point -> number;
				if (t > tmin && t < tmax) {
					if (showBoundaries) {
						Graphics_setLineType (g, Graphics_DOTTED);
						Graphics_line (g, t, -1.0, t, 1.0);   /* In sound part. */
						Graphics_setLineType (g, Graphics_DRAWN);
					}
					Graphics_line (g, t, ymin, t, 0.8 * ymin + 0.2 * ymax);
					Graphics_line (g, t, 0.2 * ymin + 0.8 * ymax, t, ymax);
					if (point -> mark)
						Graphics_text (g, t, 0.5 * (ymin + ymax), point -> mark);
				}
			}
		}
	}
	Graphics_setPercentSignIsItalic (g, TRUE);
	Graphics_setNumberSignIsBold (g, TRUE);
	Graphics_setCircumflexIsSuperscript (g, TRUE);
	Graphics_setUnderscoreIsSubscript (g, TRUE);
	Graphics_unsetInner (g);
	if (garnish) {
		Graphics_drawInnerBox (g);
		Graphics_textBottom (g, 1, U"Time (s)");
		Graphics_marksBottom (g, 2, 1, 1, 1);
	}
}

Collection TextGrid_Sound_extractAllIntervals (TextGrid me, Sound sound, long tierNumber, int preserveTimes) {
	try {
		IntervalTier tier = TextGrid_checkSpecifiedTierIsIntervalTier (me, tierNumber);
		autoCollection collection = Collection_create (NULL, tier -> numberOfIntervals ());
		for (long iseg = 1; iseg <= tier -> numberOfIntervals (); iseg ++) {
			TextInterval segment = tier -> interval (iseg);
			autoSound interval = Sound_extractPart (sound, segment -> xmin, segment -> xmax, kSound_windowShape_RECTANGULAR, 1.0, preserveTimes);
			Thing_setName (interval.peek(), segment -> text ? segment -> text : U"untitled");
			Collection_addItem (collection.peek(), interval.transfer()); 
		}
		return collection.transfer();
	} catch (MelderError) {
		Melder_throw (me, U" & ", sound, U": intervals not extracted.");
	}
}

Collection TextGrid_Sound_extractNonemptyIntervals (TextGrid me, Sound sound, long tierNumber, int preserveTimes) {
	try {
		IntervalTier tier = TextGrid_checkSpecifiedTierIsIntervalTier (me, tierNumber);
		autoCollection collection = Collection_create (NULL, tier -> numberOfIntervals ());
		for (long iseg = 1; iseg <= tier -> numberOfIntervals (); iseg ++) {
			TextInterval segment = tier -> interval (iseg);
			if (segment -> text != NULL && segment -> text [0] != '\0') {
				autoSound interval = Sound_extractPart (sound, segment -> xmin, segment -> xmax, kSound_windowShape_RECTANGULAR, 1.0, preserveTimes);
				Thing_setName (interval.peek(), segment -> text ? segment -> text : U"untitled");
				Collection_addItem (collection.peek(), interval.transfer());
			}
		}
		if (collection -> size == 0) Melder_warning (U"No non-empty intervals were found.");
		return collection.transfer();
	} catch (MelderError) {
		Melder_throw (me, U" & ", sound, U": non-empty intervals not extracted.");
	}
}

Collection TextGrid_Sound_extractIntervalsWhere (TextGrid me, Sound sound, long tierNumber,
	int comparison_Melder_STRING, const char32 *text, int preserveTimes)
{
	try {
		IntervalTier tier = TextGrid_checkSpecifiedTierIsIntervalTier (me, tierNumber);
		autoCollection collection = Collection_create (NULL, tier -> numberOfIntervals ());
		long count = 0;
		for (long iseg = 1; iseg <= tier -> numberOfIntervals (); iseg ++) {
			TextInterval segment = tier -> interval (iseg);
			if (Melder_stringMatchesCriterion (segment -> text, comparison_Melder_STRING, text)) {
				autoSound interval = Sound_extractPart (sound, segment -> xmin, segment -> xmax, kSound_windowShape_RECTANGULAR, 1.0, preserveTimes);
				Thing_setName (interval.peek(), Melder_cat (sound -> name ? sound -> name : U"", U"_", text, U"_", ++ count));
				Collection_addItem (collection.peek(), interval.transfer());
			}
		}
		if (collection -> size == 0)
			Melder_warning (U"No label that ", kMelder_string_getText (comparison_Melder_STRING), U" the text \"", text, U"\" was found.");
		return collection.transfer();
	} catch (MelderError) {
		Melder_throw (me, U" & ", sound, U": intervals not extracted.");
	}
}

static void autoMarks (Graphics g, double ymin, double ymax, bool haveDottedLines) {
	double dy = ymax - ymin;
	if (dy < 26) {
		long imin = (long) ceil ((ymin + 2.0) / 5.0), imax = (long) floor ((ymax - 2.0) / 5.0);
		for (long i = imin; i <= imax; i ++)
			Graphics_markLeft (g, i * 5, TRUE, TRUE, haveDottedLines, NULL);
	} else if (dy < 110) {
		long imin = (long) ceil ((ymin + 8.0) / 20.0), imax = (long) floor ((ymax - 8.0) / 20.0);
		for (long i = imin; i <= imax; i ++)
			Graphics_markLeft (g, i * 20, TRUE, TRUE, haveDottedLines, NULL);
	} else if (dy < 260) {
		long imin = (long) ceil ((ymin + 20.0) / 50.0), imax = (long) floor ((ymax - 20.0) / 50.0);
		for (long i = imin; i <= imax; i ++)
			Graphics_markLeft (g, i * 50, TRUE, TRUE, haveDottedLines, NULL);
	} else if (dy < 510) {
		long imin = (long) ceil ((ymin + 40.0) / 100.0), imax = (long) floor ((ymax - 40.0) / 100.0);
		for (long i = imin; i <= imax; i ++)
			Graphics_markLeft (g, i * 100, TRUE, TRUE, haveDottedLines, NULL);
	}
}

static void autoMarks_logarithmic (Graphics g, double ymin, double ymax, bool haveDottedLines) {
	double fy = ymax / ymin;
	for (int i = -12; i <= 12; i ++) {
		double power = pow (10, i), y = power;
		if (y > ymin * 1.2 && y < ymax / 1.2)
			Graphics_markLeftLogarithmic (g, y, TRUE, TRUE, haveDottedLines, NULL);
		if (fy > 2100) {
			;   /* Enough. */
		} else if (fy > 210) {
			y = 3.0 * power;
			if (y > ymin * 1.2 && y < ymax / 1.2)
				Graphics_markLeftLogarithmic (g, y, TRUE, TRUE, haveDottedLines, NULL);
		} else {
			y = 2.0 * power;
			if (y > ymin * 1.2 && y < ymax / 1.2)
				Graphics_markLeftLogarithmic (g, y, TRUE, TRUE, haveDottedLines, NULL);
			y = 5.0 * power;
			if (y > ymin * 1.2 && y < ymax / 1.2)
				Graphics_markLeftLogarithmic (g, y, TRUE, TRUE, haveDottedLines, NULL);
			if (fy < 21) {
				y = 3.0 * power;
				if (y > ymin * 1.2 && y < ymax / 1.2)
					Graphics_markLeftLogarithmic (g, y, TRUE, TRUE, haveDottedLines, NULL);
				y = 7.0 * power;
				if (y > ymin * 1.2 && y < ymax / 1.2)
					Graphics_markLeftLogarithmic (g, y, TRUE, TRUE, haveDottedLines, NULL);
			}
			if (fy < 4.1) {
				y = 1.5 * power;
				if (y > ymin * 1.2 && y < ymax / 1.2)
					Graphics_markLeftLogarithmic (g, y, TRUE, TRUE, haveDottedLines, NULL);
				y = 4.0 * power;
				if (y > ymin * 1.2 && y < ymax / 1.2)
					Graphics_markLeftLogarithmic (g, y, TRUE, TRUE, haveDottedLines, NULL);
			}
		}
	}
}

static void autoMarks_semitones (Graphics g, double ymin, double ymax, bool haveDottedLines) {
	double dy = ymax - ymin;
	if (dy < 16) {
		long imin = (long) ceil ((ymin + 1.2) / 3.0), imax = (long) floor ((ymax - 1.2) / 3.0);
		for (long i = imin; i <= imax; i ++)
			Graphics_markLeft (g, i * 3, TRUE, TRUE, haveDottedLines, NULL);
	} else if (dy < 32) {
		long imin = (long) ceil ((ymin + 2.4) / 6.0), imax = (long) floor ((ymax - 2.4) / 6.0);
		for (long i = imin; i <= imax; i ++)
			Graphics_markLeft (g, i * 6, TRUE, TRUE, haveDottedLines, NULL);
	} else if (dy < 64) {
		long imin = (long) ceil ((ymin + 4.8) / 12.0), imax = (long) floor ((ymax - 4.8) / 12.0);
		for (long i = imin; i <= imax; i ++)
			Graphics_markLeft (g, i * 12, TRUE, TRUE, haveDottedLines, NULL);
	} else if (dy < 128) {
		long imin = (long) ceil ((ymin + 9.6) / 24.0), imax = (long) floor ((ymax - 9.6) / 24.0);
		for (long i = imin; i <= imax; i ++)
			Graphics_markLeft (g, i * 24, TRUE, TRUE, haveDottedLines, NULL);
	}
}

void TextGrid_Pitch_drawSeparately (TextGrid grid, Pitch pitch, Graphics g, double tmin, double tmax,
	double fmin, double fmax, int showBoundaries, int useTextStyles, int garnish, int speckle, int unit)
{
	int ntier = grid -> tiers -> size;
	if (tmax <= tmin) tmin = grid -> xmin, tmax = grid -> xmax;
	if (Function_isUnitLogarithmic (pitch, Pitch_LEVEL_FREQUENCY, unit)) {
		fmin = Function_convertStandardToSpecialUnit (pitch, fmin, Pitch_LEVEL_FREQUENCY, unit);
		fmax = Function_convertStandardToSpecialUnit (pitch, fmax, Pitch_LEVEL_FREQUENCY, unit);
	}
	if (unit == kPitch_unit_HERTZ_LOGARITHMIC)
		Pitch_draw (pitch, g, tmin, tmax, pow (10, fmin - 0.25 * (fmax - fmin) * ntier), pow (10, fmax), FALSE, speckle, unit);
	else
		Pitch_draw (pitch, g, tmin, tmax, fmin - 0.25 * (fmax - fmin) * ntier, fmax, FALSE, speckle, unit);
	TextGrid_Sound_draw (grid, NULL, g, tmin, tmax, showBoundaries, useTextStyles, FALSE);
	/*
	 * Restore window for the sake of margin drawing.
	 */
	Graphics_setWindow (g, tmin, tmax, fmin - 0.25 * (fmax - fmin) * ntier, fmax);
	if (unit == kPitch_unit_HERTZ_LOGARITHMIC)
		fmin = pow (10, fmin), fmax = pow (10, fmax);
	if (garnish) {
		Graphics_drawInnerBox (g);
		if (unit == kPitch_unit_HERTZ_LOGARITHMIC) {
			Graphics_markLeftLogarithmic (g, fmin, TRUE, TRUE, FALSE, NULL);
			Graphics_markLeftLogarithmic (g, fmax, TRUE, TRUE, FALSE, NULL);
			autoMarks_logarithmic (g, fmin, fmax, FALSE);
		} else if (unit == kPitch_unit_SEMITONES_100) {
			Graphics_markLeft (g, fmin, TRUE, TRUE, FALSE, NULL);
			Graphics_markLeft (g, fmax, TRUE, TRUE, FALSE, NULL);
			autoMarks_semitones (g, fmin, fmax, FALSE);
		} else {
			Graphics_markLeft (g, fmin, TRUE, TRUE, FALSE, NULL);
			Graphics_markLeft (g, fmax, TRUE, TRUE, FALSE, NULL);
			autoMarks (g, fmin, fmax, FALSE);
		}
		Graphics_textLeft (g, true, Melder_cat (U"Pitch (", Function_getUnitText (pitch, Pitch_LEVEL_FREQUENCY, unit, Function_UNIT_TEXT_GRAPHICAL), U")"));
		Graphics_textBottom (g, true, U"Time (s)");
		Graphics_marksBottom (g, 2, true, true, true);
	}
}

void TextGrid_Pitch_draw (TextGrid grid, Pitch pitch, Graphics g,
	long tierNumber, double tmin, double tmax, double fmin, double fmax,
	double fontSize, int useTextStyles, int horizontalAlignment, int garnish, int speckle, int unit)
{
	try {
		Function anyTier = TextGrid_checkSpecifiedTierNumberWithinRange (grid, tierNumber);
		double oldFontSize = Graphics_inqFontSize (g);
		Pitch_draw (pitch, g, tmin, tmax, fmin, fmax, garnish, speckle, unit);
		if (tmax <= tmin) tmin = grid -> xmin, tmax = grid -> xmax;
		autoPitchTier pitchTier = Pitch_to_PitchTier (pitch);
		if (Function_isUnitLogarithmic (pitch, Pitch_LEVEL_FREQUENCY, unit)) {
			fmin = Function_convertStandardToSpecialUnit (pitch, fmin, Pitch_LEVEL_FREQUENCY, unit);
			fmax = Function_convertStandardToSpecialUnit (pitch, fmax, Pitch_LEVEL_FREQUENCY, unit);
		}
		Graphics_setTextAlignment (g, horizontalAlignment, Graphics_BOTTOM);
		Graphics_setInner (g);
		Graphics_setFontSize (g, fontSize);
		Graphics_setPercentSignIsItalic (g, useTextStyles);
		Graphics_setNumberSignIsBold (g, useTextStyles);
		Graphics_setCircumflexIsSuperscript (g, useTextStyles);
		Graphics_setUnderscoreIsSubscript (g, useTextStyles);
		if (anyTier -> classInfo == classIntervalTier) {
			IntervalTier tier = static_cast <IntervalTier> (anyTier);
			for (long i = 1; i <= tier -> numberOfIntervals (); i ++) {
				TextInterval interval = tier -> interval (i);
				double tleft = interval -> xmin, tright = interval -> xmax, tmid, f0;
				if (! interval -> text || ! interval -> text [0]) continue;
				if (tleft < pitch -> xmin) tleft = pitch -> xmin;
				if (tright > pitch -> xmax) tright = pitch -> xmax;
				tmid = (tleft + tright) / 2;
				if (tmid < tmin || tmid > tmax) continue;
				f0 = Function_convertStandardToSpecialUnit (pitch, RealTier_getValueAtTime (pitchTier.peek(), tmid), Pitch_LEVEL_FREQUENCY, unit);
				if (f0 < fmin || f0 > fmax) continue;
				Graphics_text (g,
					horizontalAlignment == Graphics_LEFT ? tleft : horizontalAlignment == Graphics_RIGHT ? tright : tmid,
					f0, interval -> text);
			}
		} else {
			TextTier tier = static_cast <TextTier> (anyTier);
			for (long i = 1; i <= tier -> numberOfPoints (); i ++) {
				TextPoint point = tier -> point (i);
				double t = point -> number, f0;
				if (! point -> mark || ! point -> mark [0]) continue;
				if (t < tmin || t > tmax) continue;
				f0 = Function_convertStandardToSpecialUnit (pitch, RealTier_getValueAtTime (pitchTier.peek(), t), Pitch_LEVEL_FREQUENCY, unit);
				if (f0 < fmin || f0 > fmax) continue;
				Graphics_text (g, t, f0, point -> mark);
			}
		}
		Graphics_setPercentSignIsItalic (g, TRUE);
		Graphics_setNumberSignIsBold (g, TRUE);
		Graphics_setCircumflexIsSuperscript (g, TRUE);
		Graphics_setUnderscoreIsSubscript (g, TRUE);
		Graphics_setFontSize (g, oldFontSize);
		Graphics_unsetInner (g);
	} catch (MelderError) {
		Melder_throw (grid, U" & ", pitch, U": not drawn.");
	}
}

/* End of file TextGrid_Sound.cpp */
