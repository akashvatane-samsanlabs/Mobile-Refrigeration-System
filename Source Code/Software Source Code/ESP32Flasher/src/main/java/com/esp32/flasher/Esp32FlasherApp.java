/*
 * Decompiled with CFR 0.152.
 */
package com.esp32.flasher;

import com.esp32.flasher.BinFileLocator;
import com.esp32.flasher.BinImageAnalyzer;
import com.esp32.flasher.EspToolRunner;
import com.esp32.flasher.FlashException;
import com.esp32.flasher.FlashValidator;
import com.esp32.flasher.SerialPortService;
import com.esp32.flasher.UiTheme;
import java.awt.BorderLayout;
import java.awt.Color;
import java.awt.Component;
import java.awt.Dimension;
import java.awt.FlowLayout;
import java.awt.Font;
import java.awt.GridBagConstraints;
import java.awt.GridBagLayout;
import java.awt.Image;
import java.awt.Insets;
import java.awt.event.ComponentAdapter;
import java.awt.event.ComponentEvent;
import java.io.File;
import java.net.URL;
import java.util.ArrayList;
import java.util.List;
import java.util.function.Consumer;
import javax.swing.BorderFactory;
import javax.swing.Box;
import javax.swing.BoxLayout;
import javax.swing.DefaultComboBoxModel;
import javax.swing.ImageIcon;
import javax.swing.JButton;
import javax.swing.JComboBox;
import javax.swing.JFileChooser;
import javax.swing.JFrame;
import javax.swing.JLabel;
import javax.swing.JOptionPane;
import javax.swing.JPanel;
import javax.swing.JScrollPane;
import javax.swing.JSplitPane;
import javax.swing.JTextArea;
import javax.swing.JTextField;
import javax.swing.SwingUtilities;
import javax.swing.SwingWorker;
import javax.swing.border.EmptyBorder;
import javax.swing.filechooser.FileNameExtensionFilter;

public class Esp32FlasherApp
extends JFrame {
    private static final int SIDEBAR_WIDTH = 320;
    private static final int SIDEBAR_PADDING = 20;
    private static final int INFO_CARD_PADDING = 28;
    private static final int FORM_LABEL_WIDTH = 155;
    private static final int COMBO_FIELD_WIDTH = 200;
    private static final int BROWSE_BUTTON_WIDTH = 88;
    private JPanel sidebarPanel;
    private JPanel logoPanel;
    private JPanel formPanel;
    private JPanel formHost;
    private JScrollPane formScroll;
    private JPanel configContentPanel;
    private JPanel importantNotePanel;
    private JLabel firmwareHintLabel;
    private JLabel stepsLabel;
    private JSplitPane mainSplitPane;
    private static final String BOOT_HINT = "If flashing fails to connect: Hold BOOT \u2192 Click Flash \u2192 Release BOOT when connecting starts.";
    private final JComboBox<String> portCombo = new JComboBox();
    private final JComboBox<String> chipCombo = new JComboBox<String>(new String[]{"esp32", "esp32s2", "esp32s3", "esp32c3", "esp8266"});
    private final JComboBox<Integer> baudCombo = new JComboBox<Integer>(new Integer[]{115200, 460800, 921600});
    private final JTextField appBinPathField = new JTextField();
    private final JTextField appOffsetField = new JTextField("0x10000", 8);
    private final JTextField bootBinPathField = new JTextField();
    private final JTextField bootOffsetField = new JTextField("0x1000", 8);
    private final JTextField partBinPathField = new JTextField();
    private final JTextField partOffsetField = new JTextField("0x8000", 8);
    private final JTextArea logArea = new JTextArea();
    private final JLabel portStatusLabel = new JLabel("No COM port selected");
    private final JLabel binInfoLabel = new JLabel("Select a .bin file to detect chip type and flash offset");
    private JButton refreshPortsButton;
    private JButton browseAppButton;
    private JButton browseBootButton;
    private JButton browsePartButton;
    private JButton flashButton;
    private File appBinFile;
    private File bootBinFile;
    private File partBinFile;
    private BinImageAnalyzer.BinImageInfo detectedAppInfo;
    private final EspToolRunner flasher = new EspToolRunner();

    public Esp32FlasherApp() {
        UiTheme.applyGlobal();
        this.initButtons();
        this.setTitle("SAMSAN ESP32 Flasher");
        this.setDefaultCloseOperation(3);
        this.setMinimumSize(new Dimension(1040, 800));
        this.setLocationRelativeTo(null);
        JPanel root = new JPanel(new BorderLayout());
        root.setBackground(UiTheme.BG_APP);
        root.add((Component)this.buildSidebar(), "West");
        root.add((Component)this.buildMainArea(), "Center");
        this.setContentPane(root);
        this.styleInputs();
        this.portCombo.setEditable(true);
        this.portCombo.setToolTipText("Select or type COM port, e.g. COM3, COM5");
        this.refreshPortsQuietly();
        this.baudCombo.setSelectedItem(921600);
        this.chipCombo.setSelectedItem("esp32");
        this.wireActions();
        this.pack();
        this.setSize(1180, 900);
        this.updateSidebarLayout();
        this.updateImportantNoteLayout();
        this.updateFormLayout();
        SwingUtilities.invokeLater(() -> {
            this.applySplitDivider();
            this.updateFormLayout();
        });
    }

    private void applySplitDivider() {
        if (this.mainSplitPane == null) {
            return;
        }
        int height = this.mainSplitPane.getHeight();
        if (height <= 0) {
            this.mainSplitPane.setDividerLocation(0.5);
            return;
        }
        this.mainSplitPane.setDividerLocation((int)((double)height * 0.5));
    }

    private void initButtons() {
        this.refreshPortsButton = UiTheme.secondaryButton("Refresh Ports");
        this.browseAppButton = UiTheme.secondaryButton("Browse");
        this.browseBootButton = UiTheme.secondaryButton("Browse");
        this.browsePartButton = UiTheme.secondaryButton("Browse");
        this.flashButton = UiTheme.primaryButton("Flash ESP32");
    }

    private void styleInputs() {
        UiTheme.styleCombo(this.portCombo);
        UiTheme.styleCombo(this.chipCombo);
        UiTheme.styleCombo(this.baudCombo);
        this.portCombo.setPrototypeDisplayValue("COM99");
        this.limitComboWidth(this.portCombo, COMBO_FIELD_WIDTH);
        this.limitComboWidth(this.chipCombo, COMBO_FIELD_WIDTH);
        this.limitComboWidth(this.baudCombo, COMBO_FIELD_WIDTH);
        UiTheme.stylePathField(this.appBinPathField);
        UiTheme.stylePathField(this.bootBinPathField);
        UiTheme.stylePathField(this.partBinPathField);
        UiTheme.styleOffsetField(this.appOffsetField);
        UiTheme.styleOffsetField(this.bootOffsetField);
        UiTheme.styleOffsetField(this.partOffsetField);
        this.fixBrowseButtonSize(this.browseAppButton);
        this.fixBrowseButtonSize(this.browseBootButton);
        this.fixBrowseButtonSize(this.browsePartButton);
        this.appBinPathField.setEditable(false);
        this.bootBinPathField.setEditable(false);
        this.partBinPathField.setEditable(false);
        UiTheme.styleLogArea(this.logArea);
    }

    private void limitComboWidth(JComboBox<?> combo, int width) {
        Dimension size = new Dimension(width, 36);
        combo.setPreferredSize(size);
        combo.setMinimumSize(new Dimension(Math.min(120, width), 36));
        combo.setMaximumSize(size);
    }

    private void fixBrowseButtonSize(JButton button) {
        Dimension size = new Dimension(BROWSE_BUTTON_WIDTH, 34);
        button.setPreferredSize(size);
        button.setMinimumSize(size);
        button.setMaximumSize(size);
        button.setMargin(new Insets(6, 10, 6, 10));
    }

    private JPanel buildSidebar() {
        this.sidebarPanel = new JPanel();
        this.sidebarPanel.setLayout(new BoxLayout(this.sidebarPanel, 1));
        this.sidebarPanel.setBackground(UiTheme.BG_SIDEBAR);
        this.sidebarPanel.setPreferredSize(new Dimension(SIDEBAR_WIDTH, 0));
        this.sidebarPanel.setMinimumSize(new Dimension(SIDEBAR_WIDTH, 0));
        this.sidebarPanel.setBorder(new EmptyBorder(20, 20, 20, 20));
        this.logoPanel = new JPanel(new BorderLayout());
        this.logoPanel.setOpaque(false);
        this.logoPanel.setAlignmentX(0.5f);
        JLabel product = this.sidebarHeading("ESP32 Flasher", 16, 1);
        JLabel subtitle = this.sidebarText("Standard Firmware Flash Tool", 12, UiTheme.TEXT_ON_DARK, true);
        JPanel orangeLine = new JPanel();
        orangeLine.setBackground(UiTheme.BRAND_ORANGE);
        orangeLine.setAlignmentX(0.5f);
        JPanel info = this.buildInfoCard();
        info.setAlignmentX(0.5f);
        JLabel tagline = this.sidebarText("in step with the NEXT", 11, new Color(255, 255, 255, 180), true, true);
        this.sidebarPanel.add(this.logoPanel);
        this.sidebarPanel.add(Box.createVerticalStrut(12));
        this.sidebarPanel.add(product);
        this.sidebarPanel.add(Box.createVerticalStrut(6));
        this.sidebarPanel.add(subtitle);
        this.sidebarPanel.add(Box.createVerticalStrut(12));
        this.sidebarPanel.add(orangeLine);
        this.sidebarPanel.add(Box.createVerticalStrut(16));
        this.sidebarPanel.add(info);
        this.sidebarPanel.add(Box.createVerticalGlue());
        this.sidebarPanel.add(tagline);
        this.sidebarPanel.addComponentListener(new ComponentAdapter(){

            @Override
            public void componentResized(ComponentEvent e) {
                Esp32FlasherApp.this.updateSidebarLayout();
            }
        });
        return this.sidebarPanel;
    }

    private int sidebarContentWidth() {
        if (this.sidebarPanel == null) {
            return 280;
        }
        return Math.max(240, this.sidebarPanel.getWidth() - 40);
    }

    private void updateSidebarLayout() {
        if (this.sidebarPanel == null) {
            return;
        }
        int contentWidth = this.sidebarContentWidth();
        this.logoPanel.removeAll();
        this.logoPanel.add((Component)this.buildLogoLabel(contentWidth), "Center");
        this.logoPanel.setMaximumSize(new Dimension(contentWidth, Integer.MAX_VALUE));
        this.logoPanel.setPreferredSize(new Dimension(contentWidth, this.logoPanel.getPreferredSize().height));
        this.logoPanel.revalidate();
        this.logoPanel.repaint();
        if (this.stepsLabel != null) {
            this.stepsLabel.setText(this.buildQuickStepsHtml(Math.max(200, contentWidth - 28)));
            this.stepsLabel.setMaximumSize(new Dimension(contentWidth - 28, Integer.MAX_VALUE));
        }
        for (Component child : this.sidebarPanel.getComponents()) {
            JPanel panel;
            JPanel panel2;
            if (child instanceof JLabel) {
                JLabel label = (JLabel)child;
                this.resizeSidebarLabel(label, contentWidth);
                continue;
            }
            if (child instanceof JPanel && (panel2 = (JPanel)child).getBackground() == UiTheme.BRAND_ORANGE) {
                panel2.setMaximumSize(new Dimension(contentWidth, 3));
                panel2.setPreferredSize(new Dimension(contentWidth, 3));
                continue;
            }
            if (!(child instanceof JPanel) || (panel = (JPanel)child) == this.logoPanel) continue;
            panel.setMaximumSize(new Dimension(contentWidth, Integer.MAX_VALUE));
        }
        this.sidebarPanel.revalidate();
        this.sidebarPanel.repaint();
    }

    private void resizeSidebarLabel(JLabel label, int contentWidth) {
        String text = label.getText();
        if (text == null || !text.startsWith("<html>")) {
            return;
        }
        text = text.replaceAll("width:\\d+px", "width:" + contentWidth + "px");
        label.setText(text);
        label.setMaximumSize(new Dimension(contentWidth, Integer.MAX_VALUE));
    }

    private JLabel sidebarHeading(String text, int size, int style) {
        int width = this.sidebarContentWidth();
        String html = "<html><body style='width:" + width + "px;text-align:center;font-family:Segoe UI;font-size:" + size + "px;font-weight:bold;color:#FFFFFF;'>" + text + "</body></html>";
        JLabel label = new JLabel(html);
        label.setFont(new Font("Segoe UI", style, size));
        label.setForeground(Color.WHITE);
        label.setAlignmentX(0.5f);
        label.setHorizontalAlignment(0);
        label.setMaximumSize(new Dimension(width, Integer.MAX_VALUE));
        return label;
    }

    private JLabel sidebarText(String text, int size, Color color, boolean centered) {
        return this.sidebarText(text, size, color, centered, false);
    }

    private JLabel sidebarText(String text, int size, Color color, boolean centered, boolean italic) {
        int width = this.sidebarContentWidth();
        String style = "width:" + width + "px;" + (centered ? "text-align:center;" : "text-align:left;") + "line-height:1.45;" + (italic ? "font-style:italic;" : "") + "color:rgb(" + color.getRed() + "," + color.getGreen() + "," + color.getBlue() + ");";
        JLabel label = new JLabel("<html><body style='" + style + "'>" + text + "</body></html>");
        label.setFont(new Font("Segoe UI", italic ? 2 : 0, size));
        label.setForeground(color);
        label.setAlignmentX(0.5f);
        label.setMaximumSize(new Dimension(width, Integer.MAX_VALUE));
        return label;
    }

    private JLabel buildLogoLabel(int contentWidth) {
        URL logoUrl = this.getClass().getResource("/images/samsan-logo.png");
        if (logoUrl != null) {
            ImageIcon icon = new ImageIcon(logoUrl);
            int logoWidth = Math.min(contentWidth - 8, 270);
            Image scaled = icon.getImage().getScaledInstance(logoWidth, -1, 4);
            JLabel label = new JLabel(new ImageIcon(scaled));
            label.setHorizontalAlignment(0);
            return label;
        }
        return this.sidebarHeading("SAMSAN", 24, 1);
    }

    private String buildQuickStepsHtml(int textWidth) {
        return "<html><body style='width:%dpx;color:#E8F0F4;line-height:1.55;font-family:Segoe UI;font-size:11px;'>\n<b style='font-size:12px;color:#FFFFFF;'>Quick Steps</b><br><br>\n1. Connect ESP32 via USB<br>\n2. Refresh and select COM port<br>\n3. Select chip type<br>\n4. Browse firmware .bin file(s)<br>\n5. Click Flash ESP32\n</body></html>\n".formatted(textWidth);
    }

    private JPanel buildInfoCard() {
        JPanel card = new JPanel(new BorderLayout());
        card.setBackground(new Color(255, 255, 255, 12));
        card.setBorder(BorderFactory.createCompoundBorder(BorderFactory.createLineBorder(new Color(255, 255, 255, 35)), new EmptyBorder(14, 14, 14, 14)));
        this.stepsLabel = new JLabel(this.buildQuickStepsHtml(Math.max(200, this.sidebarContentWidth() - 28)));
        this.stepsLabel.setBorder(null);
        card.add((Component)this.stepsLabel, "Center");
        return card;
    }

    private JPanel buildMainArea() {
        JPanel main = new JPanel(new BorderLayout(0, 0));
        main.setBackground(UiTheme.BG_APP);
        main.setBorder(new EmptyBorder(16, 16, 16, 16));
        this.mainSplitPane = UiTheme.createVerticalSplit(this.buildConfigurationSection(), this.buildLogSection());
        this.mainSplitPane.addPropertyChangeListener("dividerLocation", evt -> SwingUtilities.invokeLater(this::updateImportantNoteLayout));
        main.add((Component)this.mainSplitPane, "Center");
        return main;
    }

    private JPanel buildConfigurationPageHeader() {
        JPanel header = new JPanel();
        header.setLayout(new BoxLayout(header, 1));
        header.setOpaque(false);
        header.setBorder(new EmptyBorder(0, 0, 12, 0));
        JLabel title = new JLabel("Flash ESP32 Firmware");
        title.setFont(UiTheme.FONT_TITLE);
        title.setForeground(UiTheme.TEXT_PRIMARY);
        JLabel desc = new JLabel("Flash pre-built .bin files using esptool \u2014 no ESP-IDF required");
        desc.setFont(UiTheme.FONT_SUBTITLE);
        desc.setForeground(UiTheme.TEXT_MUTED);
        header.add(title);
        header.add(Box.createVerticalStrut(4));
        header.add(desc);
        return header;
    }

    private JPanel buildConfigurationSection() {
        this.formPanel = this.buildFormPanel();
        this.formHost = new JPanel(new BorderLayout());
        this.formHost.setOpaque(false);
        this.formHost.add((Component)this.formPanel, "North");
        this.formScroll = new JScrollPane(this.formHost);
        this.formScroll.setBorder(BorderFactory.createMatteBorder(1, 0, 0, 0, UiTheme.BORDER));
        this.formScroll.setHorizontalScrollBarPolicy(31);
        this.formScroll.getVerticalScrollBar().setUnitIncrement(16);
        this.formScroll.getViewport().setBackground(UiTheme.BG_CARD);
        this.configContentPanel = new JPanel(new BorderLayout(0, 0));
        this.configContentPanel.setOpaque(false);
        this.configContentPanel.setBackground(UiTheme.BG_CARD);
        this.configContentPanel.setBorder(new EmptyBorder(16, 16, 16, 16));
        this.configContentPanel.add((Component)this.buildConfigurationPageHeader(), "North");
        this.configContentPanel.add((Component)this.formScroll, "Center");
        this.formScroll.getViewport().addComponentListener(new ComponentAdapter(){

            @Override
            public void componentResized(ComponentEvent e) {
                Esp32FlasherApp.this.syncFormPanelWidth(Esp32FlasherApp.this.formScroll.getViewport().getWidth());
                Esp32FlasherApp.this.updateImportantNoteLayout();
            }
        });
        this.configContentPanel.addComponentListener(new ComponentAdapter(){

            @Override
            public void componentResized(ComponentEvent e) {
                Esp32FlasherApp.this.updateImportantNoteLayout();
                Esp32FlasherApp.this.updateFormLayout();
            }
        });
        this.formPanel.addComponentListener(new ComponentAdapter(){

            @Override
            public void componentResized(ComponentEvent e) {
                Esp32FlasherApp.this.updateImportantNoteLayout();
            }
        });
        JPanel section = UiTheme.borderedSection("Configuration", this.configContentPanel, false);
        section.setMinimumSize(new Dimension(200, 360));
        JPanel wrapper = new JPanel(new BorderLayout());
        wrapper.setOpaque(false);
        wrapper.add((Component)section, "Center");
        return wrapper;
    }

    private JPanel buildIntegratedImportantNote() {
        Color noteBg = new Color(250, 244, 243);
        JPanel panel = new JPanel(new BorderLayout(0, 6));
        panel.setOpaque(true);
        panel.setBackground(noteBg);
        panel.setBorder(BorderFactory.createCompoundBorder(BorderFactory.createMatteBorder(0, 3, 0, 0, UiTheme.BRAND_ORANGE), new EmptyBorder(12, 14, 12, 14)));
        JLabel title = new JLabel("Important");
        title.setFont(new Font("Segoe UI", 1, 12));
        title.setForeground(UiTheme.BRAND_TEAL);
        this.firmwareHintLabel = new JLabel(this.buildImportantNoteHtml(520));
        this.firmwareHintLabel.setVerticalAlignment(1);
        this.firmwareHintLabel.setBackground(noteBg);
        this.firmwareHintLabel.setOpaque(true);
        panel.add((Component)title, "North");
        panel.add((Component)this.firmwareHintLabel, "Center");
        return panel;
    }

    private String buildImportantNoteHtml(int width) {
        return "<html><body style='width:%dpx;font-family:Segoe UI;font-size:12px;line-height:1.65;color:#1F2A37;'>\nVAARA_V7.0_1.bin is <b>application-only</b>. Also flash from the same ESP-IDF build:<br><br>\n&bull; <b>bootloader.bin</b> @ 0x1000<br>\n&bull; <b>partition-table.bin</b> @ 0x8000<br><br>\nWithout all three files, ESP32 may show <i>flash read err, 1000</i> and fail to boot.\n</body></html>\n".formatted(width);
    }

    private void updateImportantNoteLayout() {
        if (this.firmwareHintLabel == null || this.importantNotePanel == null) {
            return;
        }
        int panelWidth = this.importantNotePanel.getWidth();
        if (panelWidth <= 0 && this.formPanel != null) {
            panelWidth = this.formPanel.getWidth();
        }
        if (panelWidth <= 0 && this.configContentPanel != null) {
            panelWidth = this.configContentPanel.getWidth() - 32;
        }
        if (panelWidth <= 0) {
            return;
        }
        int textWidth = Math.max(220, panelWidth - 32);
        this.firmwareHintLabel.setText(this.buildImportantNoteHtml(textWidth));
        this.importantNotePanel.revalidate();
        if (this.formPanel != null) {
            this.formPanel.revalidate();
        }
    }

    private JPanel buildLogSection() {
        this.logArea.setEditable(false);
        this.logArea.setRows(12);
        this.logArea.setLineWrap(true);
        this.logArea.setWrapStyleWord(true);
        JScrollPane logScroll = UiTheme.createLogScroll(this.logArea);
        JPanel logBody = new JPanel(new BorderLayout());
        logBody.setOpaque(false);
        logBody.add((Component)logScroll, "Center");
        JPanel section = UiTheme.borderedSection("Flash Log", logBody, false);
        section.setMinimumSize(new Dimension(200, 240));
        JPanel wrapper = new JPanel(new BorderLayout(0, 8));
        wrapper.setOpaque(false);
        wrapper.add((Component)section, "Center");
        JLabel hint = new JLabel("<html><b style='color:#1E6080;'>Tip:</b> If flashing fails to connect: Hold BOOT \u2192 Click Flash \u2192 Release BOOT when connecting starts.</html>");
        hint.setFont(UiTheme.FONT_SUBTITLE);
        hint.setForeground(UiTheme.TEXT_MUTED);
        hint.setBorder(new EmptyBorder(2, 4, 0, 4));
        wrapper.add((Component)hint, "South");
        return wrapper;
    }

    private void updateFormLayout() {
        if (this.formScroll != null) {
            this.syncFormPanelWidth(this.formScroll.getViewport().getWidth());
        }
        if (this.formPanel != null) {
            this.formPanel.revalidate();
            this.formPanel.repaint();
        }
    }

    private void syncFormPanelWidth(int viewportWidth) {
        if (this.formPanel == null || viewportWidth <= 0) {
            return;
        }
        int height = Math.max(this.formPanel.getPreferredSize().height, this.formPanel.getMinimumSize().height);
        Dimension width = new Dimension(viewportWidth, height);
        this.formPanel.setMaximumSize(new Dimension(viewportWidth, Integer.MAX_VALUE));
        this.formPanel.setPreferredSize(width);
        if (this.formHost != null) {
            this.formHost.setMaximumSize(new Dimension(viewportWidth, Integer.MAX_VALUE));
            this.formHost.setPreferredSize(width);
            this.formHost.revalidate();
        }
    }

    private JPanel buildFormPanel() {
        JPanel panel = new JPanel(new GridBagLayout());
        panel.setOpaque(false);
        GridBagConstraints label = new GridBagConstraints();
        label.gridx = 0;
        label.anchor = 18;
        label.insets = new Insets(8, 0, 8, 12);
        label.weightx = 0.0;
        GridBagConstraints field = new GridBagConstraints();
        field.gridx = 1;
        field.fill = 2;
        field.weightx = 1.0;
        field.insets = new Insets(8, 0, 8, 0);
        field.anchor = 18;
        int row = 0;
        this.addRow(panel, label, field, row++, "COM Port", this.buildPortPanel(), true);
        this.addRow(panel, label, field, row++, "Chip Type", this.chipCombo, false);
        this.addRow(panel, label, field, row++, "Baud Rate", this.baudCombo, false);
        this.addRow(panel, label, field, row++, "Application .bin", this.binRow(this.appOffsetField, this.appBinPathField, this.browseAppButton), true);
        this.addRow(panel, label, field, row++, "Bootloader .bin", this.binRow(this.bootOffsetField, this.bootBinPathField, this.browseBootButton), true);
        this.addRow(panel, label, field, row++, "Partition .bin", this.binRow(this.partOffsetField, this.partBinPathField, this.browsePartButton), true);
        this.binInfoLabel.setFont(UiTheme.FONT_SUBTITLE);
        this.binInfoLabel.setForeground(UiTheme.BRAND_TEAL);
        GridBagConstraints infoC = new GridBagConstraints();
        infoC.gridx = 0;
        infoC.gridy = row++;
        infoC.gridwidth = 2;
        infoC.anchor = 17;
        infoC.fill = 2;
        infoC.weightx = 1.0;
        infoC.insets = new Insets(4, 0, 0, 0);
        panel.add((Component)this.binInfoLabel, infoC);
        this.importantNotePanel = this.buildIntegratedImportantNote();
        GridBagConstraints noteC = new GridBagConstraints();
        noteC.gridx = 0;
        noteC.gridy = row++;
        noteC.gridwidth = 2;
        noteC.anchor = 17;
        noteC.fill = 2;
        noteC.weightx = 1.0;
        noteC.weighty = 0.0;
        noteC.insets = new Insets(14, 0, 0, 0);
        panel.add((Component)this.importantNotePanel, noteC);
        GridBagConstraints flashC = new GridBagConstraints();
        flashC.gridx = 0;
        flashC.gridy = row;
        flashC.gridwidth = 2;
        flashC.anchor = 17;
        flashC.insets = new Insets(14, 0, 0, 0);
        panel.add((Component)this.flashButton, flashC);
        return panel;
    }

    private JPanel buildPortPanel() {
        JPanel panel = new JPanel(new GridBagLayout());
        panel.setOpaque(false);
        GridBagConstraints refresh = new GridBagConstraints();
        refresh.gridx = 0;
        refresh.gridy = 0;
        refresh.anchor = 17;
        refresh.insets = new Insets(0, 0, 6, 8);
        GridBagConstraints combo = new GridBagConstraints();
        combo.gridx = 1;
        combo.gridy = 0;
        combo.weightx = 0.0;
        combo.fill = 0;
        combo.anchor = 17;
        combo.insets = new Insets(0, 0, 6, 0);
        GridBagConstraints status = new GridBagConstraints();
        status.gridx = 0;
        status.gridy = 1;
        status.gridwidth = 2;
        status.weightx = 1.0;
        status.fill = 2;
        status.anchor = 17;
        this.portCombo.setPrototypeDisplayValue("COM99");
        this.portStatusLabel.setFont(UiTheme.FONT_SUBTITLE);
        this.portStatusLabel.setForeground(UiTheme.TEXT_MUTED);
        panel.add((Component)this.refreshPortsButton, refresh);
        panel.add(this.portCombo, combo);
        panel.add((Component)this.portStatusLabel, status);
        return panel;
    }

    private JPanel binRow(JTextField offsetField, JTextField pathField, JButton browseButton) {
        JPanel panel = new JPanel(new BorderLayout(6, 0));
        panel.setOpaque(false);
        panel.add((Component)offsetField, "West");
        panel.add((Component)browseButton, "East");
        panel.add((Component)pathField, "Center");
        return panel;
    }

    private JPanel rowPanel(Component ... components) {
        JPanel panel = new JPanel(new FlowLayout(0, 8, 0));
        panel.setOpaque(false);
        for (Component c : components) {
            panel.add(c);
        }
        return panel;
    }

    private void addRow(JPanel panel, GridBagConstraints lc, GridBagConstraints fc, int row, String text, Component comp, boolean stretchField) {
        lc.gridy = row;
        fc.gridy = row;
        fc.gridx = 1;
        if (stretchField) {
            fc.weightx = 1.0;
            fc.fill = 2;
        } else {
            fc.weightx = 0.0;
            fc.fill = 0;
        }
        fc.anchor = 18;
        JLabel rowLabel = UiTheme.fieldLabel(text);
        rowLabel.setPreferredSize(new Dimension(FORM_LABEL_WIDTH, rowLabel.getPreferredSize().height));
        panel.add((Component)rowLabel, lc);
        panel.add(comp, fc);
    }

    private void wireActions() {
        this.refreshPortsButton.addActionListener(e -> this.refreshPorts());
        this.browseAppButton.addActionListener(e -> this.chooseBin("Application firmware", f -> {
            this.appBinFile = f;
            this.appBinPathField.setText(f.getAbsolutePath());
            this.analyzeSelectedAppBin((File)f);
        }));
        this.browseBootButton.addActionListener(e -> this.chooseBin("Bootloader", f -> {
            this.bootBinFile = f;
            this.bootBinPathField.setText(f.getAbsolutePath());
        }));
        this.browsePartButton.addActionListener(e -> this.chooseBin("Partition table", f -> {
            this.partBinFile = f;
            this.partBinPathField.setText(f.getAbsolutePath());
        }));
        this.flashButton.addActionListener(e -> this.startFlash());
    }

    private void refreshPorts() {
        this.refreshPortsQuietly();
        if (this.portCombo.getItemCount() == 0 || "(Connect ESP32 and click Refresh)".equals(this.portCombo.getItemAt(0))) {
            this.showFlashError(new FlashException(FlashException.ErrorType.NO_PORTS_FOUND));
        }
    }

    private void refreshPortsQuietly() {
        String[] ports = SerialPortService.listPortNames();
        String previous = this.getSelectedPort();
        if (ports.length == 0) {
            this.portCombo.setModel(new DefaultComboBoxModel<String>(new String[]{"(Connect ESP32 and click Refresh)"}));
            this.portCombo.setSelectedIndex(0);
            this.portStatusLabel.setText("No COM port detected \u2014 connect ESP32 USB and click Refresh Ports");
            this.portStatusLabel.setForeground(UiTheme.BRAND_ORANGE);
            this.appendLog("No COM ports found. Connect ESP32 and click Refresh Ports.");
            return;
        }
        this.portCombo.setModel(new DefaultComboBoxModel<String>(ports));
        if (previous != null) {
            for (int i = 0; i < ports.length; ++i) {
                if (!ports[i].equalsIgnoreCase(previous)) continue;
                this.portCombo.setSelectedIndex(i);
                this.updatePortStatus(ports[i]);
                this.appendLog("COM ports refreshed.");
                return;
            }
        }
        this.portCombo.setSelectedIndex(0);
        this.updatePortStatus(ports[0]);
        this.appendLog("COM ports refreshed: " + String.join(", ", ports));
    }

    private String getSelectedPort() {
        String edited;
        if (this.portCombo.getEditor() != null && this.portCombo.getEditor().getItem() != null && !(edited = this.portCombo.getEditor().getItem().toString().trim()).isEmpty() && !edited.startsWith("(")) {
            return edited;
        }
        Object selected = this.portCombo.getSelectedItem();
        if (selected == null) {
            return null;
        }
        String value = selected.toString().trim();
        if (value.startsWith("(")) {
            return null;
        }
        return value;
    }

    private void analyzeSelectedAppBin(final File binFile) {
        new SwingWorker<AnalysisResult, Void>(){

            @Override
            protected AnalysisResult doInBackground() throws Exception {
                BinImageAnalyzer.BinImageInfo info = BinImageAnalyzer.analyze(binFile);
                BinFileLocator.RelatedBins related = BinFileLocator.findRelatedBins(binFile);
                return new AnalysisResult(info, related);
            }

            @Override
            protected void done() {
                try {
                    AnalysisResult result = (AnalysisResult)this.get();
                    Esp32FlasherApp.this.detectedAppInfo = result.info();
                    Esp32FlasherApp.this.chipCombo.setSelectedItem(Esp32FlasherApp.this.detectedAppInfo.chipType());
                    Esp32FlasherApp.this.appOffsetField.setText(Esp32FlasherApp.this.detectedAppInfo.recommendedOffset());
                    if (result.related().bootloader() != null) {
                        Esp32FlasherApp.this.bootBinFile = result.related().bootloader();
                        Esp32FlasherApp.this.bootBinPathField.setText(Esp32FlasherApp.this.bootBinFile.getAbsolutePath());
                        Esp32FlasherApp.this.appendLog("Auto-found bootloader: " + Esp32FlasherApp.this.bootBinFile.getName());
                    }
                    if (result.related().partitionTable() != null) {
                        Esp32FlasherApp.this.partBinFile = result.related().partitionTable();
                        Esp32FlasherApp.this.partBinPathField.setText(Esp32FlasherApp.this.partBinFile.getAbsolutePath());
                        Esp32FlasherApp.this.appendLog("Auto-found partition table: " + Esp32FlasherApp.this.partBinFile.getName());
                    }
                    if (result.related().isComplete()) {
                        Esp32FlasherApp.this.binInfoLabel.setForeground(UiTheme.BRAND_TEAL);
                        Esp32FlasherApp.this.binInfoLabel.setText("<html><b>" + Esp32FlasherApp.this.detectedAppInfo.summary() + "</b><br>Bootloader and partition table found \u2014 ready for full flash.</html>");
                    } else {
                        Esp32FlasherApp.this.binInfoLabel.setForeground(UiTheme.BRAND_ORANGE);
                        Esp32FlasherApp.this.binInfoLabel.setText("<html><b>" + Esp32FlasherApp.this.detectedAppInfo.summary() + "</b><br><span style='color:#C0392B;'>Missing bootloader/partition files. App-only flash will NOT boot on a fresh ESP32.</span></html>");
                        Esp32FlasherApp.this.appendLog("WARNING: Only application .bin selected.");
                        Esp32FlasherApp.this.appendLog("Get bootloader.bin and partition-table.bin from the VAARA build folder.");
                    }
                    Esp32FlasherApp.this.appendLog("Use offset " + Esp32FlasherApp.this.detectedAppInfo.recommendedOffset() + " for this application .bin file.");
                }
                catch (Exception ex) {
                    Esp32FlasherApp.this.detectedAppInfo = null;
                    Esp32FlasherApp.this.binInfoLabel.setText("Could not analyze .bin \u2014 using default offset 0x10000 for ESP32 app.");
                    Esp32FlasherApp.this.appendLog("Bin analysis failed, using defaults.");
                }
            }
        }.execute();
    }

    private void updatePortStatus(String port) {
        this.portStatusLabel.setText("Selected port: " + port);
        this.portStatusLabel.setForeground(UiTheme.BRAND_TEAL);
    }

    private void chooseBin(String title, Consumer<File> onSelected) {
        JFileChooser chooser = new JFileChooser();
        chooser.setDialogTitle("Select " + title);
        chooser.setFileFilter(new FileNameExtensionFilter("Binary firmware (*.bin)", "bin"));
        chooser.setCurrentDirectory(new File(System.getProperty("user.home"), "Downloads"));
        if (chooser.showOpenDialog(this) == 0) {
            onSelected.accept(chooser.getSelectedFile());
        }
    }

    private void startFlash() {
        String port = this.getSelectedPort();
        try {
            int choice;
            FlashValidator.validatePortSelected(port);
            try {
                FlashValidator.validatePortExists(port);
            }
            catch (FlashException ex) {
                if (!port.matches("(?i)COM\\d+")) {
                    throw ex;
                }
                this.appendLog("Using manually entered port: " + port);
            }
            this.updatePortStatus(port);
            List<EspToolRunner.FlashSegment> segments = this.buildSegments();
            if (segments.isEmpty()) {
                throw new FlashException(FlashException.ErrorType.BIN_FILE_MISSING);
            }
            if (this.appBinFile != null && !this.hasBootloaderAndPartition() && (choice = JOptionPane.showConfirmDialog(this, "You are flashing APPLICATION ONLY.\n\nVAARA_V7.0_1.bin does NOT include bootloader or partition table.\nWithout bootloader.bin (0x1000) and partition-table.bin (0x8000),\nthe ESP32 will show: flash read err, 1000\n\nGet these 2 files from the same ESP-IDF build output.\nContinue anyway?", "Incomplete Flash \u2014 Will Not Boot", 0, 2)) != 0) {
                return;
            }
            int confirm = JOptionPane.showConfirmDialog(this, "Flash firmware to " + port + "?\n\nIf flashing fails to connect: Hold BOOT \u2192 Click Flash \u2192 Release BOOT when connecting starts.", "Confirm Flash", 2, 1);
            if (confirm != 0) {
                return;
            }
            final EspToolRunner.FlashOptions options = new EspToolRunner.FlashOptions(port, (String)this.chipCombo.getSelectedItem(), (Integer)this.baudCombo.getSelectedItem(), segments, false, this.detectedAppInfo != null ? this.detectedAppInfo.flashMode() : "dio", this.detectedAppInfo != null ? this.detectedAppInfo.flashFreq() : "40m", this.detectedAppInfo != null ? this.detectedAppInfo.flashSize() : "2MB");
            this.setControlsEnabled(false);
            this.logArea.setText("");
            this.appendLog("Starting flash...");
            new SwingWorker<Void, String>(){

                @Override
                protected Void doInBackground() throws Exception {
                    Esp32FlasherApp.this.flasher.flash(options, xva$0 -> this.publish(xva$0));
                    return null;
                }

                @Override
                protected void process(List<String> chunks) {
                    chunks.forEach(Esp32FlasherApp.this::appendLog);
                }

                @Override
                protected void done() {
                    Esp32FlasherApp.this.setControlsEnabled(true);
                    try {
                        this.get();
                        Esp32FlasherApp.this.appendLog("Flash completed successfully.");
                        if (Esp32FlasherApp.this.appBinFile != null && !Esp32FlasherApp.this.hasBootloaderAndPartition()) {
                            JOptionPane.showMessageDialog(Esp32FlasherApp.this, "App binary was written, but the ESP32 may NOT boot.\n\nYou still need:\n  bootloader.bin  @ 0x1000\n  partition-table.bin @ 0x8000\n\nFlash all 3 files together from the ESP-IDF build output.", "Flash Written \u2014 Boot Files Missing", 2);
                        } else {
                            JOptionPane.showMessageDialog(Esp32FlasherApp.this, "Firmware flashed successfully.", "Success", 1);
                        }
                    }
                    catch (Exception ex) {
                        Esp32FlasherApp.this.handleWorkerException(ex);
                    }
                }
            }.execute();
        }
        catch (FlashException ex) {
            this.showFlashError(ex);
        }
    }

    private List<EspToolRunner.FlashSegment> buildSegments() throws FlashException {
        ArrayList<EspToolRunner.FlashSegment> segments = new ArrayList<EspToolRunner.FlashSegment>();
        if (this.bootBinFile != null && this.bootBinFile.isFile()) {
            FlashValidator.validateOffset(this.bootOffsetField.getText().trim());
            FlashValidator.validateBinFile(this.bootBinFile);
            segments.add(new EspToolRunner.FlashSegment(this.bootOffsetField.getText().trim(), this.bootBinFile));
        }
        if (this.partBinFile != null && this.partBinFile.isFile()) {
            FlashValidator.validateOffset(this.partOffsetField.getText().trim());
            FlashValidator.validateBinFile(this.partBinFile);
            segments.add(new EspToolRunner.FlashSegment(this.partOffsetField.getText().trim(), this.partBinFile));
        }
        if (this.appBinFile != null && this.appBinFile.isFile()) {
            FlashValidator.validateOffset(this.appOffsetField.getText().trim());
            FlashValidator.validateBinFile(this.appBinFile);
            segments.add(new EspToolRunner.FlashSegment(this.appOffsetField.getText().trim(), this.appBinFile));
        }
        return segments;
    }

    private boolean hasBootloaderAndPartition() {
        return this.bootBinFile != null && this.bootBinFile.isFile() && this.partBinFile != null && this.partBinFile.isFile();
    }

    private void handleWorkerException(Exception ex) {
        Throwable cause;
        Throwable throwable = cause = ex.getCause() != null ? ex.getCause() : ex;
        if (cause instanceof InterruptedException) {
            Thread.currentThread().interrupt();
            this.showFlashError(new FlashException(FlashException.ErrorType.INTERRUPTED, cause));
            return;
        }
        if (cause instanceof FlashException) {
            FlashException flashException = (FlashException)cause;
            this.appendLog("ERROR: " + flashException.getMessage());
            this.showFlashError(flashException);
            return;
        }
        this.appendLog("ERROR: " + cause.getMessage());
        this.showFlashError(new FlashException(FlashException.ErrorType.UNKNOWN, cause));
    }

    private void showFlashError(FlashException ex) {
        JOptionPane.showMessageDialog(this, ex.getFullMessage(), "Flash Error", 0);
    }

    private void appendLog(String text) {
        SwingUtilities.invokeLater(() -> {
            this.logArea.append(text);
            this.logArea.append("\n");
            this.logArea.setCaretPosition(this.logArea.getDocument().getLength());
        });
    }

    private void setControlsEnabled(boolean enabled) {
        this.refreshPortsButton.setEnabled(enabled);
        this.browseAppButton.setEnabled(enabled);
        this.browseBootButton.setEnabled(enabled);
        this.browsePartButton.setEnabled(enabled);
        this.flashButton.setEnabled(enabled);
        this.portCombo.setEnabled(enabled);
        this.chipCombo.setEnabled(enabled);
        this.baudCombo.setEnabled(enabled);
        this.appOffsetField.setEnabled(enabled);
        this.bootOffsetField.setEnabled(enabled);
        this.partOffsetField.setEnabled(enabled);
    }

    private record AnalysisResult(BinImageAnalyzer.BinImageInfo info, BinFileLocator.RelatedBins related) {
    }
}

