#ifndef TORIRS_TITLE_PANEL_H
#define TORIRS_TITLE_PANEL_H

struct ToriRS_Sprite;

/**
 * revconfig `format=` value selecting the composite below.
 *
 * A format rather than a section of its own, because `format=` is already how
 * a [sprite:] section says which decoder its bytes want (pix8, pix32); this is
 * one more, and it reuses the whole by-name sprite path unchanged.
 */
#define TORIRS_TITLE_PANEL_FORMAT "jpeg_panel"

/**
 * The title screen's backdrop, assembled from the half-panel the cache stores.
 *
 * Both eras keep 383x503 -- half a screen -- and build the rest by mirroring
 * it: Client-TS's loadTitleBackground blits the image and its hflip, and the
 * deob blits at titleX and titleX+382. The halves share their seam column,
 * which is why 383 doubles to 765 rather than 766.
 *
 * Client-TS additionally cuts nine PixMaps out of the result because it
 * redraws only the login box each frame and leaves the other eight alone. This
 * client's emit retention gate decides that for itself, so one sprite is
 * enough and eight tiles would only be eight more things to keep in step.
 *
 * Returns NULL when the bytes are not a JPEG this decoder reads. The caller
 * owns the sprite.
 */
struct ToriRS_Sprite*
ToriRS_TitlePanelFromJpeg(
    void const* data,
    int data_size);

#endif /* TORIRS_TITLE_PANEL_H */
