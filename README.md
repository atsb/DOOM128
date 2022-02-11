# DOOM128
A DOOM 32 extension for DOS using increased limits beyond what DOOM32 provides.

# Why
Because having a DOS compatible DOOM source port that increases the limits beyond what DOOM32.EXE does is useful in playing modern WADS on DOS (where possible).

DOOM128 is a friendly fork of the amazing restoration work done here:
https://bitbucket.org/gamesrc-ver-recreation/doom

In short, it is about as vanilla as you will get and is compiled using the correct WATCOM version.

Please note, DOOM128.EXE binaries are built using the original DMX library.  You won't be able to build it if you don't have it or don't use the APODMX wrapper (see the gamesrc recreation link).

# Limits
MAXVISSPRITES    1024 * 16

SAVESTRINGSIZE 32

MAXLINEANIMS        16384 * 16

MAXPLATS    7680 * 16

MAXVISPLANES    1024 * 16

MAXOPENINGS        SCREENWIDTH*256 * 16

MAXDRAWSEGS        2048 * 16

MAXSEGS (SCREENWIDTH / 2 + 1) * SCREENHEIGHT

SAVEGAMESIZE 0x2c0000 * 16

# Credits

The most important part!  DOOM128 would not be possible without the folks below.

These two with their gamesrc recreation:

NY00123

nukeykt

https://bitbucket.org/gamesrc-ver-recreation/

Special Mentions:

PVS

ETTiNGRiNDER
