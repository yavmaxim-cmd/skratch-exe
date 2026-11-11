"""Generate the fixed Teko 500 face used for engraved hardware buttons."""
from pathlib import Path

from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont

assets = Path(__file__).resolve().parents[1] / 'Assets'
face = TTFont(assets / 'teko.ttf')
instance = instantiateVariableFont(face, {'wght': 500}, updateFontNames=True)
instance.save(assets / 'teko_hardware_medium.ttf')
