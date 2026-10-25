"""Offline source import used only by build-circuit-art.py --import-hit-sources.

Uses Arthur's established edge-aware grid and palette; never resizes a runtime cell.
The exported native sheets become reviewed inputs to the ordinary atlas build.
"""
import json
from pathlib import Path
import numpy as np
from PIL import Image
from scipy import ndimage as ndi


def import_reactions(game, source, destination):
    recipe = json.loads((game / 'art/arthur/hit-reduction.json').read_text())
    grid = recipe['grid']
    xe = np.array([grid['x'][0]-16*i for i in range(14, 0, -1)] + grid['x'] +
                  [grid['x'][-1]+16*i for i in range(1, 21)])
    ye = np.array(grid['y'])
    idle = Image.open(game / 'art/arthur/idle-approved.png').convert('RGBA').crop((0, 0, 96, 96))
    original = np.array(idle)
    palette = np.unique(original[original[:, :, 3] > 0, :3], axis=0)
    pim = Image.new('P', (1, 1))
    pim.putpalette(palette.flatten().tolist()+palette[0].tolist()*(256-len(palette)))
    destination.mkdir(parents=True, exist_ok=True)
    cells = []
    for style in ['restrained', 'exaggerated']:
        sheet = Image.new('RGBA', (288, 96))
        for n, pose in enumerate(['impact', 'recoil', 'recovery']):
            name = f'{style}-{pose}'
            im = Image.open(source / f'{name}.png').convert('RGBA')
            assert abs(im.width-1448) <= 2 and abs(im.height-1086) <= 2, im.size
            canvas = Image.new('RGBA', (1448, 1086)); canvas.paste(im, (0, 0))
            ar = np.array(canvas)
            labels, _ = ndi.label(ar[:, :, 3] >= 192)
            sizes = np.bincount(labels.ravel()); sizes[0] = 0
            mask = labels == sizes.argmax()
            ar[:, :, 3] = mask*255; ar[~mask, :3] = 0
            bottom = np.where(mask)[0].max()
            dy = int(1044-bottom)
            aligned = np.zeros_like(ar)
            if dy > 0: aligned[dy:] = ar[:-dy]
            elif dy < 0: aligned[:dy] = ar[-dy:]
            else: aligned = ar
            ar = aligned; mask = ar[:, :, 3] > 0
            # Register the planted feet to the same source-space centre. This
            # is a translation, not an independent scale-to-fit of each pose.
            foot_x = np.where(mask[979:1045])[1]
            dx = round(recipe['foot_center_x']-(int(foot_x.min())+int(foot_x.max()))/2)
            aligned = np.zeros_like(ar)
            if dx > 0: aligned[:, dx:] = ar[:, :-dx]
            elif dx < 0: aligned[:, :dx] = ar[:, -dx:]
            else: aligned = ar
            ar = aligned; mask = ar[:, :, 3] > 0
            qi = np.asarray(Image.fromarray(ar[:, :, :3]).quantize(palette=pim, dither=Image.Dither.NONE))
            qi = np.where(qi < len(palette), qi, 0)
            native = np.zeros((64, len(xe)-1, 4), np.uint8)
            for y, (ya, yb) in enumerate(zip(ye[:-1], ye[1:])):
                for x, (xa, xb) in enumerate(zip(xe[:-1], xe[1:])):
                    m = mask[ya:yb, xa:xb]
                    if m.mean() < .43: continue
                    yy, xx = np.mgrid[:yb-ya, :xb-xa]
                    weights = .5+np.exp(-((xx/(xb-xa)-.5)**2+(yy/(yb-ya)-.5)**2)/.13)
                    counts = np.bincount(qi[ya:yb, xa:xb][m], weights=weights[m], minlength=len(palette))
                    colour = ar[ya:yb, xa:xb, :3][m].mean(0)*.65+palette[counts.argmax()]*.35
                    native[y, x, :3] = palette[((palette.astype(float)-colour)**2).sum(1).argmin()]
                    native[y, x, 3] = 255
            cell = Image.new('RGBA', (96, 96)); cell.paste(Image.fromarray(native), (8, 16))
            for xy, colour in recipe.get('pixel_corrections', {}).get(name, []):
                cell.putpixel(tuple(xy), tuple(colour))
            if recipe.get('planted_from_row') is not None:
                row = recipe['planted_from_row']
                cell.paste(idle.crop((0, row, 96, 96)), (0, row))
            cell.save(destination / f'{name}.png')
            sheet.paste(cell, (n*96, 0)); cells.append(cell)
        sheet.save(destination / f'hit-{style}-v1.png')
    review = Image.new('RGB', (384, 192), '#293541')
    for row in range(2):
        for col, cell in enumerate([idle]+cells[row*3:row*3+3]):
            review.paste(cell, (col*96, row*96), cell)
    review.save(destination / 'review-1x.png')
    review.resize((1536, 768), Image.Resampling.NEAREST).save(destination / 'review-4x.png')
