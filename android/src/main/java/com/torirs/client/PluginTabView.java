package com.torirs.client;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.view.View;

/**
 * The phone's way into the plugins: a small tab in the screen's top-right
 * corner.
 *
 * No plugin and no CS2 interface carries a plugin button any more; the
 * platform does. On a desk that is a sidebar beside the game, and a phone has
 * no width to spare for one, so here it is this tab -- the same shape as the
 * mobile web page's corner tab -- and a tap toggles the client's plugin window,
 * which is drawn in the game canvas with the plugin buttons in it.
 *
 * Drawn rather than typed, for the reason GearView gives: a chevron glyph is a
 * tofu box on this project's API 22 target.
 */
final class PluginTabView extends View
{
    private final Paint plate = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mark = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path chevron = new Path();
    private final RectF box = new RectF();
    private final int width;
    private final int height;

    PluginTabView(Context context, int widthPx, int heightPx)
    {
        super(context);
        this.width = widthPx;
        this.height = heightPx;
        plate.setColor(Color.argb(200, 20, 16, 12));
        mark.setColor(Color.rgb(224, 213, 184));
        mark.setStyle(Paint.Style.STROKE);
        mark.setStrokeCap(Paint.Cap.ROUND);
        mark.setStrokeJoin(Paint.Join.ROUND);
        setClickable(true);
        setContentDescription("Plugins");
    }

    @Override
    protected void onMeasure(int widthSpec, int heightSpec)
    {
        setMeasuredDimension(width, height);
    }

    @Override
    protected void onDraw(Canvas canvas)
    {
        final float w = getWidth();
        final float h = getHeight();
        final float radius = w * 0.45f;

        plate.setAlpha(isPressed() ? 255 : 200);
        /* Rounded on the bottom-left only: the tab is flush with the screen's
         * top and right edges. */
        box.set(0f, -radius, w + radius, h);
        canvas.drawRoundRect(box, radius, radius, plate);

        /* A left-pointing chevron: "pull the plugins out from this side". */
        mark.setStrokeWidth(Math.max(2f, w * 0.12f));
        chevron.reset();
        chevron.moveTo(w * 0.62f, h * 0.36f);
        chevron.lineTo(w * 0.38f, h * 0.50f);
        chevron.lineTo(w * 0.62f, h * 0.64f);
        canvas.drawPath(chevron, mark);
    }
}
