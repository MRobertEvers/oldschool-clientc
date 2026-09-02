package com.torirs.client;

import android.content.Context;
import android.graphics.Rect;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;

/**
 * The Activity's application layout: game surface and the one shared plugin
 * page are siblings in the same native window.
 *
 * <p>The breakpoint is deliberately a measurement, not a device or orientation
 * test. Android windows can change size while the Activity stays alive (split
 * screen, desktop mode, folds and rotation). When both useful regions fit, the
 * page sits beside the game. Otherwise it owns the content area and the game
 * SurfaceView becomes {@link View#GONE}; that makes the normal Surface callback
 * publish a null ANativeWindow while the native game loop keeps running.</p>
 */
public final class PluginChromeLayout extends ViewGroup
{
    public interface EditorFocusListener
    {
        void onPluginEditorFocusChanged(boolean focused);
    }

    private static final int RAIL_DP = 56;
    private static final int PANEL_DP = 320;
    private static final int PANEL_MIN_DP = 280;
    private static final int GAME_MIN_DP = 480;

    private final View gameView;
    private final View overlayView;
    private View railView;
    private View paneView;
    private boolean chromeOpen;
    private boolean split;
    private boolean pluginEditorFocused;
    private EditorFocusListener editorFocusListener;
    private final Rect gameRect = new Rect();

    private int insetLeft;
    private int insetTop;
    private int insetRight;
    private int insetBottom;

    public PluginChromeLayout(Context context, View gameView, View overlayView)
    {
        super(context);
        this.gameView = gameView;
        this.overlayView = overlayView;
        setClipChildren(true);
        addView(gameView);
        addView(overlayView);
    }

    public void attachChrome(View rail, View pane)
    {
        if( railView != null || paneView != null )
            throw new IllegalStateException("plugin chrome is already attached");
        railView = rail;
        paneView = pane;
        railView.setVisibility(GONE);
        paneView.setVisibility(GONE);
        /* Added after the SurfaceView, so an opaque native pane is above it
         * during the one layout pass in which an expanded window becomes
         * exclusive. It never relies on that overlap after layout settles. */
        addView(railView);
        addView(paneView);
    }

    public void setEditorFocusListener(EditorFocusListener listener)
    {
        editorFocusListener = listener;
    }

    public void setPluginEditorFocused(boolean focused)
    {
        if( pluginEditorFocused == focused )
            return;
        pluginEditorFocused = focused;
        if( editorFocusListener != null )
            editorFocusListener.onPluginEditorFocusChanged(focused);
    }

    public boolean isPluginEditorFocused()
    {
        return pluginEditorFocused;
    }

    public void setChromeOpen(boolean open)
    {
        if( chromeOpen == open )
            return;
        chromeOpen = open;
        if( !open )
            setPluginEditorFocused(false);
        requestLayout();
    }

    public boolean isChromeOpen()
    {
        return chromeOpen;
    }

    public boolean isSplit()
    {
        return chromeOpen && split;
    }

    /** Insets are applied only to application chrome. The game retains the
     * Surface geometry and keyboard-inset path it already owns in native code. */
    public void setChromeInsets(int left, int top, int right, int bottom)
    {
        left = Math.max(0, left);
        top = Math.max(0, top);
        right = Math.max(0, right);
        bottom = Math.max(0, bottom);
        if( insetLeft == left && insetTop == top && insetRight == right && insetBottom == bottom )
            return;
        insetLeft = left;
        insetTop = top;
        insetRight = right;
        insetBottom = bottom;
        applyChromeInsets();
    }

    private void applyChromeInsets()
    {
        if( railView != null )
            railView.setPadding(insetLeft, insetTop, 0, insetBottom);
        if( paneView != null )
            paneView.setPadding(0, insetTop, insetRight, insetBottom);
    }

    private int dp(int value)
    {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    private static void measureExactly(View view, int width, int height)
    {
        view.measure(
                MeasureSpec.makeMeasureSpec(Math.max(0, width), MeasureSpec.EXACTLY),
                MeasureSpec.makeMeasureSpec(Math.max(0, height), MeasureSpec.EXACTLY));
    }

    @Override
    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec)
    {
        int width = MeasureSpec.getSize(widthMeasureSpec);
        int height = MeasureSpec.getSize(heightMeasureSpec);
        int railWidth = Math.min(dp(RAIL_DP), width);
        int panelWidth = Math.min(dp(PANEL_DP), Math.max(0, width - railWidth));
        boolean nextSplit = chromeOpen
                && width >= dp(GAME_MIN_DP + RAIL_DP + PANEL_MIN_DP);

        split = nextSplit;
        if( chromeOpen && railView != null && paneView != null )
        {
            railView.setVisibility(VISIBLE);
            paneView.setVisibility(VISIBLE);
            if( split )
            {
                gameView.setVisibility(VISIBLE);
                int gameWidth = Math.max(0, width - railWidth - panelWidth);
                measureExactly(gameView, gameWidth, height);
                measureExactly(railView, railWidth, height);
                measureExactly(paneView, panelWidth, height);
                gameRect.set(0, 0, gameWidth, height);
            }
            else
            {
                /* GONE, rather than a zero-sized visible SurfaceView: surface
                 * loss is Android's supported lifetime signal and the native
                 * presenter already treats a null window as "skip present". */
                gameView.setVisibility(GONE);
                measureExactly(gameView, 0, 0);
                measureExactly(railView, railWidth, height);
                measureExactly(paneView, Math.max(0, width - railWidth), height);
                gameRect.setEmpty();
            }
        }
        else
        {
            if( railView != null )
                railView.setVisibility(GONE);
            if( paneView != null )
                paneView.setVisibility(GONE);
            gameView.setVisibility(VISIBLE);
            measureExactly(gameView, width, height);
            if( railView != null )
                measureExactly(railView, 0, 0);
            if( paneView != null )
                measureExactly(paneView, 0, 0);
            gameRect.set(0, 0, width, height);
        }

        /* The keyboard dismiss affordance belongs to the game region, not the
         * plugin page. Its own measured size is capped by that region. */
        overlayView.measure(
                MeasureSpec.makeMeasureSpec(Math.max(0, gameRect.width()), MeasureSpec.AT_MOST),
                MeasureSpec.makeMeasureSpec(Math.max(0, gameRect.height()), MeasureSpec.AT_MOST));
        setMeasuredDimension(width, height);
    }

    @Override
    protected void onLayout(boolean changed, int left, int top, int right, int bottom)
    {
        int width = right - left;
        int height = bottom - top;

        if( chromeOpen && railView != null && paneView != null )
        {
            int railWidth = railView.getMeasuredWidth();
            if( split )
            {
                int gameWidth = gameView.getMeasuredWidth();
                gameView.layout(0, 0, gameWidth, height);
                railView.layout(gameWidth, 0, gameWidth + railWidth, height);
                paneView.layout(gameWidth + railWidth, 0, width, height);
            }
            else
            {
                gameView.layout(0, 0, 0, 0);
                railView.layout(0, 0, railWidth, height);
                paneView.layout(railWidth, 0, width, height);
            }
        }
        else
        {
            gameView.layout(0, 0, width, height);
            if( railView != null )
                railView.layout(0, 0, 0, 0);
            if( paneView != null )
                paneView.layout(0, 0, 0, 0);
        }

        int overlayWidth = overlayView.getMeasuredWidth();
        int overlayHeight = overlayView.getMeasuredHeight();
        overlayView.layout(
                Math.max(gameRect.left, gameRect.right - overlayWidth),
                gameRect.top,
                gameRect.right,
                Math.min(gameRect.bottom, gameRect.top + overlayHeight));
    }

    /** Absorb blank-space touches in rail/page so they can never fall through
     * Activity.onTouchEvent and be translated into game-surface coordinates. */
    @Override
    public boolean onTouchEvent(MotionEvent event)
    {
        return chromeOpen && !gameRect.contains((int)event.getX(), (int)event.getY());
    }
}
