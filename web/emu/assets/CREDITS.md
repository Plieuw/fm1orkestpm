# Image credits

Two photographs, both checked against the source's own licence metadata on
24 November 2026. Everything else on `web/emu/index.html` is drawn by CSS.

They are deliberately small: the hall is only ever seen out of focus behind the
instrument, and `hardware.css` blurs and darkens it further, so 320 × 180 is
enough — and it keeps the repository light.

## `hall.jpg` — the backdrop

- **Work:** *Great Hall of the Musikverein, Vienna (9)*
- **Author:** Michał Bulsa (Wikimedia Commons user [Lahcim n](https://commons.wikimedia.org/wiki/User:Lahcim_n))
- **Source:** <https://commons.wikimedia.org/wiki/File:Great_Hall_of_the_Musikverein,_Vienna_(9).jpg>
- **Licence:** [CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) — the file's
  metadata says `CC BY-SA 4.0` / `Creative Commons Attribution-Share Alike 4.0`,
  own work, taken 2 September 2022.
- **Changes:** cropped to 16:9 (ceiling and stage kept, floor cut), resized to
  320 × 180, Gaussian blurred 1.6 px, brightness +15 %, JPEG quality 32
  (4.5 KB). The page then blurs it 2.5 px and dims it under a gradient.
- **What that obliges:** credit as above, a link to the licence, and an
  indication that it was changed — all of which this file is. The cropped,
  softened picture stays under CC BY-SA 4.0; it is not relicensed under the
  project's GPL-3.0-or-later. The two works are distributed together, the GPL
  covering the code and this licence covering this file, which is what the
  licence allows.

## `wood.jpg` — the walnut cheeks

- **Work:** *WoodFloor064* (colour map), ambientCG
- **Author:** Lennart Demes / ambientCG
- **Source:** <https://ambientcg.com/view?id=WoodFloor064>
- **Licence:** [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/) —
  no attribution required, credited anyway.
- **Changes:** resized to a 96 × 96 tile, JPEG quality 68 (1 KB).

## Checking a licence again

The Commons API reports the licence in the file's own metadata, so it can be
re-verified without opening the page:

```sh
curl -s -G https://commons.wikimedia.org/w/api.php \
  --data-urlencode action=query --data-urlencode format=json \
  --data-urlencode "titles=File:Great Hall of the Musikverein, Vienna (9).jpg" \
  --data-urlencode prop=imageinfo --data-urlencode iiprop=extmetadata \
  | python3 -c 'import json,sys; m=json.load(sys.stdin)["query"]["pages"]; \
    print([v["imageinfo"][0]["extmetadata"]["LicenseShortName"]["value"] for v in m.values()])'
```

## What was rejected, and why

- **CC BY-NC / CC BY-ND** concert-hall photographs: no commercial use, or no
  derivatives — both rule out a project that is redistributed and modified.
- **No licence at all** (venue press photographs, most "wallpaper" sites): not
  an option, however good the picture.
- **Stock sites with their own terms** (Unsplash, Pexels, Pixabay): usable, but
  their licences are not a named Creative Commons licence and their terms can be
  revised; the two files above were unambiguous, so they won.
- **Other CC BY-SA concert halls** (Concertgebouw, Elbphilharmonie, Konzerthaus):
  also fine, just not as good a view — the Musikverein from the audience shows
  seats, ceiling and stage at once.
