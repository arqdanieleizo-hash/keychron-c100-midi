# Turn on MIDI and VIA (VIA is what the Keychron Launcher uses).
# Do not add anything else here:
#   NKRO_ENABLE = no  -> Keychron's code no longer builds
#   a DEBOUNCE setting -> builds, but no key presses register
MIDI_ENABLE = yes
VIA_ENABLE  = yes
LTO_ENABLE = yes

# The C100 chip has 256 KB of flash, but the board files say 128 KB. With 128 KB the saved settings
# (Launcher keymap) sit at 124,928 bytes, and a firmware bigger than that erases its own end on the
# first start (v12 did this). These lines use all 256 KB and move the saved settings to the very end.
MCU_LDSCRIPT = AT32F405xC
OPT_DEFS += -DAT32_FLASH_SECTORS_PER_BANK=256 -DWEAR_LEVELING_EFL_FLASH_SIZE=262144
