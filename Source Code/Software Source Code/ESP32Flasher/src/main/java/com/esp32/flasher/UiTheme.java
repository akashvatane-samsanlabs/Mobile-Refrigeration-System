/*
 * Decompiled with CFR 0.152.
 */
package com.esp32.flasher;

import java.awt.BorderLayout;
import java.awt.Color;
import java.awt.Component;
import java.awt.Cursor;
import java.awt.Dimension;
import java.awt.Font;
import java.awt.Graphics;
import java.awt.Graphics2D;
import java.awt.Insets;
import java.awt.RenderingHints;
import javax.swing.BorderFactory;
import javax.swing.JButton;
import javax.swing.JCheckBox;
import javax.swing.JComboBox;
import javax.swing.JComponent;
import javax.swing.JLabel;
import javax.swing.JPanel;
import javax.swing.JScrollPane;
import javax.swing.JSplitPane;
import javax.swing.JTextArea;
import javax.swing.JTextField;
import javax.swing.UIManager;
import javax.swing.border.EmptyBorder;

public final class UiTheme {
    public static final Color BRAND_TEAL = new Color(30, 96, 128);
    public static final Color BRAND_TEAL_DARK = new Color(21, 74, 99);
    public static final Color BRAND_ORANGE = new Color(241, 90, 36);
    public static final Color BG_APP = new Color(240, 244, 248);
    public static final Color BG_CARD = Color.WHITE;
    public static final Color BG_SIDEBAR = new Color(23, 63, 85);
    public static final Color TEXT_PRIMARY = new Color(31, 42, 55);
    public static final Color TEXT_MUTED = new Color(107, 114, 128);
    public static final Color TEXT_ON_DARK = new Color(232, 240, 244);
    public static final Color BORDER = new Color(216, 226, 234);
    public static final Color LOG_BG = new Color(14, 20, 28);
    public static final Color LOG_TEXT = new Color(240, 246, 252);
    public static final Font FONT_TITLE = new Font("Segoe UI", 1, 20);
    public static final Font FONT_SUBTITLE = new Font("Segoe UI", 0, 12);
    public static final Font FONT_LABEL = new Font("Segoe UI Semibold", 1, 12);
    public static final Font FONT_BODY = new Font("Segoe UI", 0, 12);
    public static final Font FONT_BUTTON = new Font("Segoe UI Semibold", 1, 12);
    public static final Font FONT_MONO = new Font("Consolas", 0, 13);

    private UiTheme() {
    }

    public static void applyGlobal() {
        UIManager.put("ComboBox.font", FONT_BODY);
        UIManager.put("TextField.font", FONT_BODY);
        UIManager.put("CheckBox.font", FONT_BODY);
        UIManager.put("TabbedPane.font", FONT_BODY);
    }

    public static JPanel card() {
        JPanel panel = new JPanel();
        panel.setBackground(BG_CARD);
        panel.setBorder(BorderFactory.createCompoundBorder(BorderFactory.createLineBorder(BORDER), new EmptyBorder(16, 16, 16, 16)));
        return panel;
    }

    public static JLabel sectionTitle(String text) {
        JLabel label = new JLabel(text);
        label.setFont(FONT_LABEL);
        label.setForeground(BRAND_TEAL);
        return label;
    }

    public static JLabel fieldLabel(String text) {
        JLabel label = new JLabel(text);
        label.setFont(FONT_BODY);
        label.setForeground(TEXT_PRIMARY);
        return label;
    }

    public static void styleField(JTextField field) {
        field.setFont(FONT_BODY);
        field.setBackground(Color.WHITE);
        field.setForeground(TEXT_PRIMARY);
        field.setBorder(BorderFactory.createCompoundBorder(BorderFactory.createLineBorder(BORDER), new EmptyBorder(8, 10, 8, 10)));
        field.setPreferredSize(new Dimension(Math.max(field.getPreferredSize().width, 80), 34));
        field.setMinimumSize(new Dimension(80, 34));
    }

    public static void styleOffsetField(JTextField field) {
        UiTheme.styleField(field);
        field.setPreferredSize(new Dimension(92, 34));
        field.setMinimumSize(new Dimension(92, 34));
        field.setMaximumSize(new Dimension(92, 34));
    }

    public static void stylePathField(JTextField field) {
        field.setFont(FONT_BODY);
        field.setBackground(Color.WHITE);
        field.setForeground(TEXT_PRIMARY);
        field.setBorder(BorderFactory.createCompoundBorder(BorderFactory.createLineBorder(BORDER), new EmptyBorder(8, 10, 8, 10)));
        field.setPreferredSize(new Dimension(10, 34));
        field.setMinimumSize(new Dimension(10, 34));
        field.setMaximumSize(new Dimension(Integer.MAX_VALUE, 34));
    }

    public static void styleCombo(JComboBox<?> combo) {
        combo.setFont(FONT_BODY);
        combo.setBackground(Color.WHITE);
        combo.setForeground(TEXT_PRIMARY);
        combo.setPreferredSize(new Dimension(200, 36));
        combo.setMinimumSize(new Dimension(120, 36));
    }

    public static void styleCheck(JCheckBox check) {
        check.setFont(FONT_BODY);
        check.setBackground(BG_CARD);
        check.setForeground(TEXT_PRIMARY);
    }

    public static JButton primaryButton(String text) {
        return UiTheme.styledButton(text, BRAND_ORANGE, Color.WHITE, true);
    }

    public static JButton secondaryButton(String text) {
        return UiTheme.styledButton(text, BRAND_TEAL, Color.WHITE, false);
    }

    public static JButton sidebarButton(String text) {
        return UiTheme.styledButton(text, new Color(0, 0, 0, 0), TEXT_ON_DARK, false);
    }

    public static JButton sidebarButtonActive(String text) {
        return UiTheme.styledButton(text, BRAND_ORANGE, Color.WHITE, true);
    }

    public static JPanel sectionHeaderBar(String title) {
        JPanel header = new JPanel(new BorderLayout());
        header.setBackground(new Color(232, 240, 246));
        header.setBorder(BorderFactory.createCompoundBorder(BorderFactory.createMatteBorder(0, 0, 1, 0, BORDER), new EmptyBorder(10, 14, 10, 14)));
        header.add((Component)UiTheme.sectionTitle(title), "West");
        return header;
    }

    public static JPanel borderedSection(String title, JComponent body, boolean scrollable) {
        JPanel section = new JPanel(new BorderLayout(0, 0));
        section.setBackground(BG_CARD);
        section.setBorder(BorderFactory.createLineBorder(BORDER));
        section.add((Component)UiTheme.sectionHeaderBar(title), "North");
        if (scrollable) {
            JScrollPane scroll = new JScrollPane(body);
            scroll.setBorder(BorderFactory.createEmptyBorder());
            scroll.setHorizontalScrollBarPolicy(31);
            scroll.getVerticalScrollBar().setUnitIncrement(16);
            scroll.getViewport().setBackground(BG_CARD);
            section.add((Component)scroll, "Center");
        } else {
            section.add((Component)body, "Center");
        }
        return section;
    }

    public static JSplitPane createVerticalSplit(JComponent top, JComponent bottom) {
        JSplitPane split = new JSplitPane(0, top, bottom);
        split.setDividerSize(6);
        split.setContinuousLayout(true);
        split.setOneTouchExpandable(false);
        split.setResizeWeight(0.5);
        split.setBorder(BorderFactory.createMatteBorder(4, 0, 4, 0, BG_APP));
        split.setBackground(BG_APP);
        return split;
    }

    public static void styleLogArea(JTextArea area) {
        area.setFont(FONT_MONO);
        area.setBackground(LOG_BG);
        area.setForeground(LOG_TEXT);
        area.setCaretColor(BRAND_ORANGE);
        area.setBorder(new EmptyBorder(14, 16, 14, 16));
        area.setMargin(new Insets(0, 0, 0, 0));
    }

    public static JScrollPane createLogScroll(JTextArea area) {
        UiTheme.styleLogArea(area);
        JScrollPane scroll = new JScrollPane(area);
        scroll.setBorder(BorderFactory.createEmptyBorder());
        scroll.setHorizontalScrollBarPolicy(31);
        scroll.setVerticalScrollBarPolicy(22);
        scroll.getVerticalScrollBar().setUnitIncrement(18);
        scroll.getViewport().setBackground(LOG_BG);
        return scroll;
    }

    public static JScrollPane styledScroll(JComponent view, String title) {
        JScrollPane scroll = new JScrollPane(view);
        scroll.setBorder(BorderFactory.createTitledBorder(BorderFactory.createLineBorder(BORDER), title, 1, 2, FONT_LABEL, BRAND_TEAL));
        scroll.getViewport().setBackground(view.getBackground());
        return scroll;
    }

    private static JButton styledButton(String text, final Color bg, Color fg, final boolean filled) {
        JButton button = new JButton(text){

            @Override
            protected void paintComponent(Graphics g) {
                Graphics2D g2 = (Graphics2D)g.create();
                g2.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);
                Color paint = bg;
                if (!this.isEnabled()) {
                    paint = new Color(180, 180, 180);
                } else if (this.getModel().isRollover() && filled) {
                    paint = bg.brighter();
                } else if (this.getModel().isRollover() && bg.getAlpha() == 0) {
                    paint = new Color(255, 255, 255, 30);
                } else if (this.getClientProperty("navActive") == Boolean.TRUE) {
                    paint = BRAND_ORANGE;
                } else if (bg.getAlpha() == 0) {
                    paint = new Color(0, 0, 0, 0);
                }
                if (paint.getAlpha() > 0) {
                    g2.setColor(paint);
                    g2.fillRoundRect(0, 0, this.getWidth(), this.getHeight(), 10, 10);
                }
                g2.dispose();
                super.paintComponent(g);
            }
        };
        button.setFont(FONT_BUTTON);
        button.setForeground(fg);
        button.setFocusPainted(false);
        button.setBorderPainted(false);
        button.setContentAreaFilled(false);
        button.setOpaque(false);
        button.setCursor(Cursor.getPredefinedCursor(12));
        button.setMargin(new Insets(10, 16, 10, 16));
        if (!filled) {
            button.setBorder(BorderFactory.createEmptyBorder(10, 16, 10, 16));
        }
        return button;
    }
}

