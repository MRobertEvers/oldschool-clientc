package com.torirs.client;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.view.View;

/**
 * A gear, drawn rather than typed.
 *
 * The obvious spelling is the character U+2699 in a TextView, and on this
 * project's target device that renders as a tofu box: Android 5.1's bundled
 * font has no glyph for it, and a missing glyph is not a fallback, it is a
 * rectangle where the button should be. Emoji and dingbat coverage is exactly
 * the kind of thing that varies between an API 22 phone and a modern one, so
 * the icon is geometry instead -- eight teeth and a hole, which every Canvas
 * has been able to draw since API 1.
 */
final class GearView extends View
{
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final int size;

    GearView(Context context, int sizePx)
    {
        super(context);
        this.size = sizePx;
        paint.setColor(Color.rgb(150, 200, 255));
        setClickable(true);
        setContentDescription("Edit profiles");
    }

    @Override
    protected void onMeasure(int widthSpec, int heightSpec)
    {
        setMeasuredDimension(size, size);
    }

    @Override
    protected void onDraw(Canvas canvas)
    {
        final float cx = getWidth() / 2f;
        final float cy = getHeight() / 2f;
        /* Leave a little air so the teeth are not clipped by the view bounds. */
        final float r = Math.min(cx, cy) * 0.72f;
        final float toothLen = r * 0.42f;
        final float toothWide = r * 0.34f;

        /* Pressed state, so the button feels like one without a background
         * drawable (this app carries no resources beyond a string). */
        paint.setAlpha(isPressed() ? 140 : 255);

        paint.setStyle(Paint.Style.FILL);
        for( int i = 0; i < 8; i++ )
        {
            canvas.save();
            canvas.rotate(i * 45f, cx, cy);
            canvas.drawRect(cx - toothWide / 2f, cy - r - toothLen,
                            cx + toothWide / 2f, cy - r + toothLen * 0.4f, paint);
            canvas.restore();
        }
        canvas.drawCircle(cx, cy, r, paint);

        /* The hub: punched out by drawing the background colour over it, which
         * is cheaper and steadier than a PorterDuff layer for one circle. */
        paint.setStyle(Paint.Style.FILL);
        int keep = paint.getColor();
        paint.setColor(Color.BLACK);
        canvas.drawCircle(cx, cy, r * 0.38f, paint);
        paint.setColor(keep);
    }

    @Override
    public void setPressed(boolean pressed)
    {
        super.setPressed(pressed);
        invalidate();
    }
}
