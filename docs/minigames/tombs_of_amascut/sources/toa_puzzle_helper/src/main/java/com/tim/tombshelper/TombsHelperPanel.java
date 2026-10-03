package com.tim.tombshelper;

import com.tim.tombshelper.TombsHelperPlugin.PuzzleRoom;

import net.runelite.client.ui.ColorScheme;
import net.runelite.client.ui.PluginPanel;
import net.runelite.client.util.ImageUtil;

import lombok.extern.slf4j.Slf4j;

import javax.swing.*;
import javax.swing.border.EmptyBorder;
import java.awt.*;
import java.awt.image.BufferedImage;

@Slf4j
public class TombsHelperPanel extends PluginPanel {

    private final JTabbedPane jtp = new JTabbedPane();
    private final JLabel title = new JLabel();

    //Kephri
    private final JPanel imageKephriLightSwitches = new JPanel(new GridBagLayout());
    private final JPanel imageKephriSum = new JPanel(new GridBagLayout());
    private static final BufferedImage kephriLightSwitchesBImg = ImageUtil.loadImageResource(TombsHelperPlugin.class, "/com/tim/tombshelper/puzzles/kephri_light_switches.png");
    private static final BufferedImage kephriSumBImg = ImageUtil.loadImageResource(TombsHelperPlugin.class, "/com/tim/tombshelper/puzzles/kephri_sums.png");
    private static final JLabel kephriLightSwitchesLabel = new JLabel(new ImageIcon(kephriLightSwitchesBImg));
    private static final JLabel kephriSumLabel = new JLabel(new ImageIcon(kephriSumBImg));

    //Baba
    private final JPanel imageBabaWaveSolo = new JPanel(new GridBagLayout());
    private static final BufferedImage babaWaveSoloBImg = ImageUtil.loadImageResource(TombsHelperPlugin.class, "/com/tim/tombshelper/puzzles/baba_wave_spawns_solo.png");
    private static final JLabel babaWaveSoloLabel = new JLabel(new ImageIcon(babaWaveSoloBImg));


    private static GridBagConstraints constraints = new GridBagConstraints();
    public TombsHelperPanel(TombsHelperPlugin plugin) {

        setLayout(new BorderLayout());
        setBorder(new EmptyBorder(10, 10, 10, 10));

        JPanel northPanel = new JPanel(new BorderLayout());
        northPanel.setBorder(new EmptyBorder(1, 0, 10, 0));

        title.setText("Tombs of Amascut Helper");
        title.setForeground(Color.WHITE);

        northPanel.add(title, BorderLayout.CENTER);

        JPanel centerPanel = new JPanel(new BorderLayout());
        centerPanel.setBackground(ColorScheme.DARK_GRAY_COLOR);

        jtp.setBackground(ColorScheme.DARK_GRAY_COLOR);

        constraints.fill = GridBagConstraints.HORIZONTAL;
        constraints.weightx = 1;
        constraints.gridx = 0;
        constraints.gridy = 0;

        imageKephriLightSwitches.add(kephriLightSwitchesLabel, constraints);
        imageKephriSum.add(kephriSumLabel, constraints);

        imageBabaWaveSolo.add(babaWaveSoloLabel, constraints);


        centerPanel.add(jtp);

        add(northPanel, BorderLayout.NORTH);
        add(centerPanel, BorderLayout.CENTER);
    }

    public void update(PuzzleRoom currentRoom) {
        SwingUtilities.invokeLater(() -> updatePanel(currentRoom));
    }

    private void updatePanel(PuzzleRoom currentRoom) {
        jtp.removeAll();
        title.setText("Tombs of Amascut Helper");
        jtp.setBounds(0, 0, 200, 500);
        switch(currentRoom) {
            case KEPHRI:
                title.setText("Kephri Helper");
                jtp.add("Lights", imageKephriLightSwitches);
                jtp.add("Sum", imageKephriSum);
                break;
            case BABA:
                title.setText("Ba-Ba Helper");
                jtp.add("Solo", imageBabaWaveSolo);
                break;
            default:
                break;
        }
        repaint();
        revalidate();
    }
}
