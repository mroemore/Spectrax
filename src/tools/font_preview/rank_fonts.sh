#!/bin/sh
# rank_fonts.sh - rank Spectrax's pixel fonts against a rendering profile.
#
# Port of the former rank_fonts.py to POSIX sh (the project's no-Python
# tooling rule). Same semantics; the only dropped feature is the
# --no-raylib/fontTools APPROXIMATE fallback, which required the fontTools
# Python package. The raylib probe (fontprobe.c) is ground truth — stb_truetype
# rasterization is what the app actually renders with — so no information is
# lost by requiring it.
#
# Profile semantics (defaults derived from the raylib bundled default font,
# the fallback behind every font-less DrawText() in Spectrax + siblings):
#
#   GetFontDefault()  ->  baseSize 10, 224 glyphs on a 128x128 texture,
#                         glyph cell 10px x 10px (ink <= 9x10 + 1px advance).
#   profile = (maxW, maxH) = (10, 10); target (comparable) size = 10.
#
# Measurement: each TTF/OTF is rasterized by raylib itself (fontprobe.c) at
# every integer size in a sweep band around the target; the best size is the
# fitting size whose glyph box is closest to the profile, width-first.  PNG
# sprite fonts are measured once (their cell is fixed by the image).
#
# Ranking: acceptable fonts (fit inside the profile box at their best size)
# rank above unacceptable ones. A perfect match (dW==0 && dH==0) is rank 1;
# after that closeness in width outranks closeness in height:
#   key = (|maxW - PW|, |maxH - PH|, |bestSize - target|, maxAdv)
#
# Usage:
#   sh rank_fonts.sh [--spectrax ROOT] [--max-w N] [--max-h N]
#                    [--target N] [--sizes FROM-TO] [--out FILE]
#                    [--display :99]
#
# Requirements: gcc + the vendored raylib under <spectrax>/include and
# <spectrax>/lib/linux, and an X display (Xvfb works) for raylib's GL context.
# The probe binary is cached in ~/.cache/spectrax-font-profile/ and rebuilt
# when fontprobe.c changes.

set -u

TAB=$(printf '\t')

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

# --- defaults -------------------------------------------------------------
SPECTRAX=""
MAX_W=10
MAX_H=10
TARGET=10
SIZES_LO=8
SIZES_HI=16
OUT=""
DISPLAY_OVERRIDE=""

# --- arg parsing ----------------------------------------------------------
while [ "$#" -gt 0 ]; do
	case "$1" in
		--spectrax) SPECTRAX="$2"; shift 2 ;;
		--max-w) MAX_W="$2"; shift 2 ;;
		--max-h) MAX_H="$2"; shift 2 ;;
		--target) TARGET="$2"; shift 2 ;;
		--sizes)
			SIZES_LO=${2%-*}; SIZES_HI=${2#*-}; shift 2 ;;
		--out) OUT="$2"; shift 2 ;;
		--display) DISPLAY_OVERRIDE="$2"; shift 2 ;;
		--no-raylib)
			echo "rank_fonts.sh: --no-raylib (fontTools APPROX fallback) is not supported in the sh port; the raylib probe is required." >&2
			exit 1 ;;
		*)
			echo "rank_fonts.sh: unknown argument: $1" >&2
			exit 1 ;;
	esac
done

if [ -z "$SPECTRAX" ]; then
	SPECTRAX="$SCRIPT_DIR/../.."
fi
if [ -z "$OUT" ]; then
	OUT="$SCRIPT_DIR/spectrax-font-profile-${MAX_W}x${MAX_H}.md"
fi

# --- discovery ------------------------------------------------------------
# *.ttf/*.TTF/*.otf/*.OTF anywhere; *.png only under dirs named fonts/font.
FONTS=$( {
	find "$SPECTRAX" \( -name '*.ttf' -o -name '*.TTF' -o -name '*.otf' -o -name '*.OTF' \) -type f
	find "$SPECTRAX" \( -iname 'fonts' -o -iname 'font' \) -type d -exec find {} -maxdepth 1 -name '*.png' -type f \;
} 2>/dev/null | while IFS= read -r f; do
	case "$f" in
		/*) printf '%s\n' "$f" ;;
		*) printf '%s\n' "$PWD/$f" ;;
	esac
done | sort -u )

if [ -z "$FONTS" ]; then
	echo "rank_fonts.sh: no fonts found under $SPECTRAX" >&2
	exit 1
fi

# --- probe ----------------------------------------------------------------
INC="$SPECTRAX/include"
LIB="$SPECTRAX/lib/linux"
PROBE_SRC="$SCRIPT_DIR/fontprobe.c"
if [ ! -f "$PROBE_SRC" ] || [ ! -d "$INC" ] || [ ! -d "$LIB" ]; then
	echo "rank_fonts.sh: vendored raylib not found under $SPECTRAX (need include/ + lib/linux/)" >&2
	exit 1
fi
if ! command -v gcc >/dev/null 2>&1 && ! command -v cc >/dev/null 2>&1; then
	echo "rank_fonts.sh: no C compiler (gcc/cc) available" >&2
	exit 1
fi
CC=$(command -v gcc 2>/dev/null || command -v cc)

CACHE="${XDG_CACHE_HOME:-$HOME/.cache}/spectrax-font-profile"
mkdir -p "$CACHE"
PROBE="$CACHE/fontprobe"
if [ ! -x "$PROBE" ] || [ "$PROBE" -ot "$PROBE_SRC" ]; then
	$CC "$PROBE_SRC" -I"$INC" -L"$LIB" -lraylib -lm -o "$PROBE" || {
		echo "rank_fonts.sh: failed to build the fontprobe harness" >&2
		exit 1
	}
fi

export DISPLAY="${DISPLAY_OVERRIDE:-${DISPLAY:-}}"

# --- measurement ----------------------------------------------------------
# One row per (font, size): path<TAB>kind<TAB>size<TAB>base<TAB>glyphs<TAB>maxW<TAB>maxH<TAB>maxAdv
ROWS="$CACHE/rows.$$"
: > "$ROWS"
trap 'rm -f "$ROWS"' EXIT

printf '%s\n' "$FONTS" | while IFS= read -r font; do
	kind=ttf
	case "$font" in
		*.png|*.PNG) kind=png ;;
	esac
	if [ "$kind" = png ]; then
		out=$("$PROBE" "$font" 0 2>/dev/null)
		line=$(printf '%s\n' "$out" | sed -n 's/.*| png | [0-9]* | base=\([0-9]*\) | glyphs=\([0-9]*\) | maxW=\([0-9]*\) | maxH=\([0-9]*\) | maxAdv=\([0-9]*\).*/\1 \2 \3 \4 \5/p')
		if [ -n "$line" ]; then
			set -- $line
			printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$font" "$kind" 0 "$1" "$2" "$3" "$4" "$5" >> "$ROWS"
		fi
	else
		s=$SIZES_LO
		while [ "$s" -le "$SIZES_HI" ]; do
			out=$("$PROBE" "$font" "$s" 2>/dev/null)
			line=$(printf '%s\n' "$out" | sed -n 's/.*| ttf | [0-9]* | base=\([0-9]*\) | glyphs=\([0-9]*\) | maxW=\([0-9]*\) | maxH=\([0-9]*\) | maxAdv=\([0-9]*\).*/\1 \2 \3 \4 \5/p')
			if [ -n "$line" ]; then
				set -- $line
				printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$font" "$kind" "$s" "$1" "$2" "$3" "$4" "$5" >> "$ROWS"
			fi
			s=$((s + 1))
		done
	fi
done

if [ ! -s "$ROWS" ]; then
	echo "rank_fonts.sh: no font measurements succeeded (probe produced no output)" >&2
	exit 1
fi

# --- per-font best + ranking ----------------------------------------------
# Emit tab-separated ranked lines for the markdown table. awk does the
# best-size selection (fitting preferred, width-first) and classification.
awk -F'\t' -v PW="$MAX_W" -v PH="$MAX_H" -v TGT="$TARGET" '
function abs(x){ return x<0 ? -x : x }
{
	path=$1; kind=$2; size=$3; base=$4; glyphs=$5; maxW=$6; maxH=$7; maxAdv=$8;
	key=path;
	if (!(key in seen)) { order[n++]=key; seen[key]=1; kinds[key]=kind; glyphCounts[key]=glyphs; }
	dW=abs(PW-maxW); dH=abs(PH-maxH); dsize=abs(TGT-size);
	fits = (maxW<=PW && maxH<=PH);
	# best: prefer fitting; within the pool min by (dW,dH,dsize,maxAdv)
	if (kind=="png") {
		# single row; classification later
		best[key]=maxW" "maxH" "size" "maxAdv" "dW" "dH" "dsize" "fits" "glyphs" "base;
		fitsAny[key] = fits;
	} else {
		pool = fits ? 1 : 0;
		k = dW*1000000 + dH*10000 + dsize*100 + maxAdv;
		if (pool==1) {
			if (!(key in bFit) || k < bFit[key]) {
				bFit[key]=k; bFitW[key]=maxW; bFitH[key]=maxH; bFitS[key]=size;
				bFitA[key]=maxAdv; bFitD[key]=dsize;
			}
		} else {
			if (!(key in bAll) || k < bAll[key]) {
				bAll[key]=k; bAllW[key]=maxW; bAllH[key]=maxH; bAllS[key]=size;
				bAllA[key]=maxAdv; bAllD[key]=dsize;
			}
		}
		if (fits) hasFit[key]=1;
	}
}
END {
	for (i=0;i<n;i++) {
		key=order[i];
		fits = (hasFit[key]==1);
		if (kinds[key]=="png") {
			# best is the single row; fits from the row
			split(best[key],a," ");
			maxW=a[1]; maxH=a[2]; size=a[3]; maxAdv=a[4]; dW=a[5]; dH=a[6]; dsize=a[7]; fits=(a[8]==1); glyphs=a[9]; base=a[10];
			# classify png: icon (few glyphs) / fallback (default font returned) / text
			cls="text";
			if (glyphs<32) cls="icon";
			if (glyphs==224 && maxW==9 && maxH==10 && base==10) cls="fallback";
			if (cls=="icon" || cls=="fallback") { excludedCnt++; excluded[excludedCnt]=key"\t"cls; continue; }
		} else {
			if (fits) {
				maxW=bFitW[key]; maxH=bFitH[key]; size=bFitS[key]; maxAdv=bFitA[key]; dsize=bFitD[key];
			} else {
				maxW=bAllW[key]; maxH=bAllH[key]; size=bAllS[key]; maxAdv=bAllA[key]; dsize=bAllD[key];
			}
			dW=abs(PW-maxW); dH=abs(PH-maxH);
		}
		# build sortable line: accepted first (fits=1), then (dW,dH,dsize,maxAdv)
		sortkey = sprintf("%d%04d%04d%04d%04d", fits, dW, dH, dsize, maxAdv);
		printf "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%d\t%d\t%d\t%d\n", sortkey, key, kinds[key], size, maxW, maxH, dW, dH, dsize, maxAdv, fits, glyphs;
	}
	for (i=1;i<=excludedCnt;i++) print "EXCLUDED\t" excluded[i];
}' "$ROWS" | sort -n > "$CACHE/ranked.$$"

# --- markdown -------------------------------------------------------------
RANKED="$CACHE/ranked.$$"
trap 'rm -f "$ROWS" "$RANKED"' EXIT

acc=0
rej=0
excl=$(grep -c '^EXCLUDED' "$RANKED" || true)
markdown="$OUT"
{
	echo "# Spectrax pixel fonts vs. the raylib default-font profile"
	echo ""
	echo "- **Profile (max width x max height allowable):** \`$MAX_W x $MAX_H\` px"
	echo "- **Comparable size (target render size):** \`$TARGET\` px"
	echo "- **Measurement engine:** raylib rasterization (fontprobe.c harness)"
	echo "- **Size sweep:** sizes \`$SIZES_LO..$SIZES_HI\`; the best size per font is the fitting size closest to the profile (width-first)"
	echo "- **Spectrax root scanned:** \`$SPECTRAX\`"
	echo ""
	echo "## Profile derivation"
	echo ""
	echo "raylib's bundled default font (\`GetFontDefault()\`) is the fallback used by every "
	echo "\`DrawText()\` call that does not pass an explicit \`Font\` — in Spectrax that is "
	echo "\`spectrogram.c:50\`, \`graph_gui.c:213\`, \`modvisual.c\`, \`dataviz.c:65\` and "
	echo "\`gui.c:2301,2420,2435\`. It is a 10px-tall, 224-glyph bitmap whose glyphs occupy at most "
	echo "9x10 px in a 10px-wide x 10px-tall cell (advance = glyph width + 1), which is where the "
	echo "default \`${MAX_W}x${MAX_H}\` profile comes from. A replacement font is acceptable iff, at some "
	echo "comparable render size, its rasterized glyph box fits inside \`$MAX_W x $MAX_H\` px; any font "
	echo "that fills the box exactly (\`${MAX_W}x${MAX_H}\`) is a perfect match, and otherwise closeness in "
	echo "**width** outranks closeness in **height**."
	echo ""
	echo "## Ranking (ordered by acceptability)"
	echo ""
	echo "| # | font | kind | best size | glyph box (W x H) | vs profile |"
	echo "|---|------|------|-----------|-------------------|------------|"

	while IFS="$TAB" read -r sk path kind size maxW maxH dW dH dsize maxAdv fits glyphs; do
		case "$sk" in
			EXCLUDED)
				# skip (handled below)
				;;
			*)
				if [ "$fits" = 1 ]; then
					acc=$((acc + 1))
					echo "| $acc | $(basename -- "$path") | $kind | $size | ${maxW}x${maxH} | dW=$dW dH=$dH |"
				fi
				;;
		esac
	done < "$RANKED"

	echo ""
	echo "### Not acceptable (exceed the profile box)"
	echo ""
	echo "| # | font | kind | size | glyph box (W x H) | vs profile |"
	echo "|---|------|------|------|-------------------|------------|"

	while IFS="$TAB" read -r sk path kind size maxW maxH dW dH dsize maxAdv fits glyphs; do
		if [ "$fits" = 0 ] && [ "$sk" != EXCLUDED ]; then
			rej=$((rej + 1))
			echo "| $rej | $(basename -- "$path") | $kind | $size | ${maxW}x${maxH} | over W by $dW, over H by $dH |"
		fi
	done < "$RANKED"

	if grep -q '^EXCLUDED' "$RANKED"; then
		echo ""
		echo "### Excluded from ranking (not usable as text fonts via raylib)"
		echo ""
		echo "| font | reason |"
		echo "|------|--------|"
		while IFS="$TAB" read -r sk path cls; do
			case "$sk" in EXCLUDED)
				if [ "$cls" = icon ]; then
					echo "| $(basename -- "$path") | icon sheet (not a text font) |"
				else
					echo "| $(basename -- "$path") | raylib could not parse sheet; returned the bundled default font |"
				fi
				;;
			esac
		done < "$RANKED"
	fi

	echo ""
	echo "## Per-font notes"
	echo ""
	while IFS="$TAB" read -r sk path kind size maxW maxH dW dH dsize maxAdv fits glyphs; do
		case "$sk" in EXCLUDED) continue ;; esac
		note=""
		case "$path" in
			*console.ttf*) note=", current Spectrax default pixelFont" ;;
			*sample_analyser*) note=", sample-analyser tool font" ;;
		esac
		if [ "$fits" = 1 ]; then verdict="acceptable"; else verdict="too large for the profile"; fi
		echo "- **$(basename -- "$path")** ($kind, box ${maxW}x${maxH} at size $size) — $verdict$note"
	done < "$RANKED"

	echo ""
	echo "## How to use"
	echo '```'
	echo "sh rank_fonts.sh --spectrax $SPECTRAX --max-w $MAX_W --max-h $MAX_H --target $TARGET --out $OUT"
	echo '```'
	echo "Tune \`--max-w/--max-h/--target\` to the sibling app's actual pixel box and render size."
	echo ""
	echo "## Caveats"
	echo "- PNG sprite fonts cannot be resized without losing pixel-perfectness; their cell is fixed "
	echo "by the image (e.g. all 128x128 sheets here are 10x12 px or larger and exceed the 10x10 profile)."
	echo "- Icon sheets (\`iconzfin.png\`, \`synthicons.png\`, \`iconz.png\`, \`iconplay.png\`) are not text "
	echo "fonts; raylib reports default-font metrics for the 72px-wide sheets (fallback) — they are excluded "
	echo "from the ranked text list where ambiguous."
	echo "- Vector pixel-style TTFs (e.g. \`04B_03__.TTF\`, \`Daydream.ttf\`) are crispest at integer multiples "
	echo "of their design grid; the chosen 'best size' is the closest fitting size to the target."
} > "$markdown"

echo "wrote $OUT: $acc acceptable, $rej not acceptable, $excl excluded"
exit 0