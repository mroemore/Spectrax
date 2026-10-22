#ifndef THEME_H
#define THEME_H

#include "raylib.h"

typedef struct {
	Color backgroundColor;
	Color secondaryFontColour;
	Color fontColour;
	Color outlineColour;
	Color defaultCell;
	Color blankCell;
	Color highlightedCell;
	Color selectedCell;
	Color reddish;
	Color panel;
	Color panelBorder;
	Color valueDisplayBg;
	Color label;
	Color labelSelected;
	Color dial;
	Color valueText;
	/* Dimmed text for controls/values that exist but are not currently
	 * available (e.g. a macro that only applies to some voice types). */
	Color disabledText;
	Color vline;
	Color poly;
	Color waveformBg;
	Color waveform;
	Color waveformAlt;
	Color sampleBg;
	Color sampleAltBg;
	Color sampleBorder;
	Color stepBorder;
	Color stepClosed;
	Color arrangerPlayhead;
	Color arrangerCellText;
	Color wrapperBorder;
	Color modStripLfo;
	Color modStripEnv;
	Color modStripRnd;
	Color modStripOfs;
	Color modStripDefault;
	Color layerDim;
	Color spectrogramPlayhead;
	Color routeAdd;
	Color routeMul;
	Color chipPalette0;
	Color chipPalette1;
	Color chipPalette2;
	Color chipPalette3;
	Color chipPalette4;
	Color chipPalette5;
	Color chipPalette6;
	Color chipPalette7;
} ColourScheme;

typedef struct {
	char path[256];
	int size;
	int spacing;
} FontConfig;

#endif
