# Resources and the stylesheet

Icons, images, data files and your stylesheet are **compiled into the app**
-- nothing to install beside it, nothing to go missing. Add one from the
project list's **+** (or `mortimer-gen add`); `mortimer-gen resources` shows
what's bundled and the name your code uses:

| Kind | Code uses |
|---|---|
| icon (`*-symbolic.svg`) | its name: `star-symbolic` -- any `icon_name` property, recoloured to the theme |
| image | its resource path: `/io/github/you/Notes/images/cover.svg` |
| data file | its resource path: `/io/github/you/Notes/data/words.txt` |
| stylesheet | `resources/<app>.css` -- loaded for every window, nothing to call |

Rename a resource from the project list's pencil: forms that name the icon
follow. The bin moves it to `removed/`.

## The stylesheet

File menu ▸ **Edit Stylesheet…** opens it beside the canvas: every keystroke restyles the canvas live, a mistake is
underlined with its line. **Save** writes it; the canvas always wears it.

Give widgets **CSS classes** in the inspector -- the chips offer your
stylesheet's classes and GTK's own (`suggested-action`, `card`, `dim-label`
...).
