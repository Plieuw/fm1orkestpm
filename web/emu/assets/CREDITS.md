# Artwork and credits

There are no photographs in `web/emu/assets/` and no third-party images on the
page: both files are vector drawings made for this project, and everything else
on `web/emu/index.html` is CSS.

| file | what it is | licence |
| --- | --- | --- |
| `hall.svg` | the Musikverein's Great Hall: gilded coffered ceiling, arcade of lit windows, panelled dado, the organ's pipes on the stage, rows of red seats with aisles and brass rails, chandeliers, a warm spotlight and a vignette | GPL-3.0-or-later, like the rest of the repository |
| `wood.svg` | a 256 px seamless walnut-grain tile for the cheeks of the front panel | GPL-3.0-or-later |

They are drawn to be seen the way the page shows them: `hardware.css` blurs the
hall 1.6 px and dims it behind the instrument, so the artwork is built out of
large shapes and a warm palette rather than detail. Open the SVG on its own in
a browser to see it undistorted.

## Why vectors instead of the photographs

The first version used two photographs — the Great Hall by Michał Bulsa
(CC BY-SA 4.0) and a walnut floor from ambientCG (CC0) — with the licences,
authors and changes recorded here. Both were allowed, but CC BY-SA is a
share-alike licence on the image, and the upload route for this branch carries
text only, so the files had to stay very small. Drawing the two images removed
both problems at once: nothing to attribute, and a few kilobytes of crisp art
instead of a blurry photograph.

The old files and their credit text remain in the history if they are ever
wanted again (`git log -- web/emu/assets`).

## What a licence check looks like, if images are added later

This is the one that was done, and it still works:

```sh
curl -s -G https://commons.wikimedia.org/w/api.php \
  --data-urlencode action=query --data-urlencode format=json \
  --data-urlencode "titles=File:Great Hall of the Musikverein, Vienna (9).jpg" \
  --data-urlencode prop=imageinfo --data-urlencode iiprop=extmetadata \
  | python3 -c 'import json,sys; m=json.load(sys.stdin)["query"]["pages"]; \
    print([v["imageinfo"][0]["extmetadata"]["LicenseShortName"]["value"] for v in m.values()])'
```

And what was rejected then, for the same reason it would be rejected now:

- **CC BY-NC / CC BY-ND**: no commercial use, or no derivatives — neither fits
  a project that is redistributed and modified.
- **No licence at all**: venue press photographs, most "wallpaper" sites.
- **Stock sites with their own terms** (Unsplash, Pexels, Pixabay): usable, but
  not a named Creative Commons licence and their terms can be revised.
