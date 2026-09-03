package com.torirs.client;

import android.content.Context;
import android.graphics.Rect;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;

/** Game surface plus exactly one persistent local plugin-chrome WebView. */
public final class PluginChromeLayout extends ViewGroup
{
    public interface EditorFocusListener
    {
        void onPluginEditorFocusChanged(boolean focused);
    }

    /** Matches legacy-ie8.css, the explicit Chrome-39/API22 bundle. */
    private static final int RAIL_DP = 46;
    private static final int PANEL_DP = 320;
    private static final int PANEL_MIN_DP = 280;
    private static final int PANEL_MAX_DP = 480;
    private static final int GAME_MIN_DP = 480;

    private final View gameView;
    private final View overlayView;
    private View chromeView;
    private boolean chromeOpen;
    private boolean split;
    private boolean pluginEditorFocused;
    private int panelWidthDp = PANEL_DP;
    private EditorFocusListener editorFocusListener;
    private final Rect gameRect = new Rect();

    public PluginChromeLayout(Context context, View gameView, View overlayView)
    {
        super(context);
        this.gameView = gameView;
        this.overlayView = overlayView;
        setClipChildren(true);
        addView(gameView);
        addView(overlayView);
    }

    public void attachChrome(View chrome)
    {
        if( chromeView != null )
            throw new IllegalStateException("plugin chrome is already attached");
        chromeView = chrome;
        chromeView.setVisibility(VISIBLE);
        addView(chromeView);
        requestLayout();
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

    public boolean isPluginEditorFocused() { return pluginEditorFocused; }

    public void setChromeOpen(boolean open)
    {
        if( chromeOpen == open )
            return;
        chromeOpen = open;
        if( !open )
            setPluginEditorFocused(false);
        requestLayout();
    }

    public boolean isChromeOpen() { return chromeOpen; }
    public boolean isSplit() { return chromeOpen && split; }

    public void setPanelWidthDp(int width)
    {
        int next = Math.max(PANEL_MIN_DP, Math.min(PANEL_MAX_DP, width));
        if( panelWidthDp == next )
            return;
        panelWidthDp = next;
        requestLayout();
    }

    public boolean isGameVisible()
    {
        return gameView.getVisibility() == VISIBLE && gameRect.width() > 0 &&
                gameRect.height() > 0;
    }

    public void setChromeInsets(int left, int top, int right, int bottom)
    {
        if( chromeView != null )
            chromeView.setPadding(
                    Math.max(0, left), Math.max(0, top),
                    Math.max(0, right), Math.max(0, bottom));
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
        int collapsed = Math.min(width, dp(RAIL_DP));
        int expanded = Math.min(width, dp(RAIL_DP + panelWidthDp));
        split = chromeOpen && width >= dp(GAME_MIN_DP + RAIL_DP + PANEL_MIN_DP);

        if( chromeView == null )
        {
            gameView.setVisibility(VISIBLE);
            measureExactly(gameView, width, height);
            gameRect.set(0, 0, width, height);
        }
        else if( !chromeOpen )
        {
            gameView.setVisibility(VISIBLE);
            measureExactly(gameView, Math.max(0, width - collapsed), height);
            measureExactly(chromeView, collapsed, height);
            gameRect.set(0, 0, Math.max(0, width - collapsed), height);
        }
        else if( split )
        {
            gameView.setVisibility(VISIBLE);
            measureExactly(gameView, Math.max(0, width - expanded), height);
            measureExactly(chromeView, expanded, height);
            gameRect.set(0, 0, Math.max(0, width - expanded), height);
        }
        else
        {
            gameView.setVisibility(GONE);
            measureExactly(gameView, 0, 0);
            measureExactly(chromeView, width, height);
            gameRect.setEmpty();
        }

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
        int gameWidth = gameView.getMeasuredWidth();
        gameView.layout(0, 0, gameWidth, gameView.getMeasuredHeight());
        if( chromeView != null )
        {
            int chromeWidth = chromeView.getMeasuredWidth();
            int chromeLeft = chromeOpen && !split ? 0 : width - chromeWidth;
            chromeView.layout(chromeLeft, 0, chromeLeft + chromeWidth, height);
        }

        int overlayWidth = overlayView.getMeasuredWidth();
        int overlayHeight = overlayView.getMeasuredHeight();
        overlayView.layout(
                Math.max(gameRect.left, gameRect.right - overlayWidth),
                gameRect.top,
                gameRect.right,
                Math.min(gameRect.bottom, gameRect.top + overlayHeight));
    }

    @Override
    public boolean onTouchEvent(MotionEvent event)
    {
        if( event.getActionMasked() == MotionEvent.ACTION_UP )
            performClick();
        return chromeView != null && !gameRect.contains((int)event.getX(), (int)event.getY());
    }

    @Override
    public boolean performClick()
    {
        super.performClick();
        return true;
    }
}
