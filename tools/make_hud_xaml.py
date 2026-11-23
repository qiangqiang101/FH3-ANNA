"""Makes media/UI/Controls/HUDAnnaControl.xaml from the game's own copy.

ANNA shows a list of up to 7 entries whole; a longer one shows 6 at a time, scrolling between two marker rows,
which ANNA labels ANNA_MORE_UP, ANNA_MORE_DOWN and ANNA_MORE_NONE (src/menu.c). This patches the control's row
template to draw those as a scroll arrow (the pause menu's own), or nothing, on a shorter row that never shows the
highlight.

Usage: python -I tools/make_hud_xaml.py <UI.zip of FH3 1.0.125.0> <output .xaml>
  e.g. python -I tools/make_hud_xaml.py "D:/Games/FH2/Opus_1.0.125.0_x64__8wekyb3d8bbwe/Mount/media/UI.zip" \
           media/UI/Controls/HUDAnnaControl.xaml
"""
import sys
import zipfile

MEMBER = "Controls/HUDAnnaControl.xaml"
MARKER_HEIGHT = 22          # a marker row (the entries' are 36)


def main(zip_path, out_path):
    raw = zipfile.ZipFile(zip_path).read(MEMBER).decode("utf-8-sig")
    crlf = "\r\n" in raw
    t = raw.replace("\r\n", "\n")

    def sub(old, new):
        nonlocal t
        if t.count(old) != 1:
            raise SystemExit("the game's XAML isn't the one this was written for (%d x %r)" % (t.count(old), old[:70]))
        t = t.replace(old, new)

    # The row: the entry's text, and the two arrows (down; and up, turned round) on the same bar.
    sub('''                    <local:FZMaterial x:Name="Item" Style="{StaticResource A_Style_FZMaterial_GlassHudItem}">
                        <local:FZTextBlock Text="{Binding Command}" Style="{StaticResource A_Style_FZTextBlock_HudMedium}" Margin="8,0,8,0" VerticalAlignment="Center" HorizontalAlignment="Right" Width="{StaticResource A_Double_SpeechOptionWidth}" />
                    </local:FZMaterial>''',
        '''                    <local:FZMaterial x:Name="Item" Style="{StaticResource A_Style_FZMaterial_GlassHudItem}">
                        <Grid>
                            <local:FZTextBlock x:Name="Label" Text="{Binding Command}" Style="{StaticResource A_Style_FZTextBlock_HudMedium}" Margin="8,0,8,0" VerticalAlignment="Center" HorizontalAlignment="Right" Width="{StaticResource A_Double_SpeechOptionWidth}" />
                            <local:FZMaterial x:Name="MoreUp" Visibility="Collapsed" ImageSource="/ForzaUI;component/Textures/Anthem/SocialFeedIcons/Scroll_Arrow.png" Width="34" Height="16" HorizontalAlignment="Center" VerticalAlignment="Center" RenderTransformOrigin="0.5,0.5">
                                <local:FZMaterial.RenderTransform><RotateTransform Angle="180"/></local:FZMaterial.RenderTransform>
                            </local:FZMaterial>
                            <local:FZMaterial x:Name="MoreDown" Visibility="Collapsed" ImageSource="/ForzaUI;component/Textures/Anthem/SocialFeedIcons/Scroll_Arrow.png" Width="34" Height="16" HorizontalAlignment="Center" VerticalAlignment="Center"/>
                        </Grid>
                    </local:FZMaterial>''')

    # The markers: shorter, never highlighted, no button; after the highlight's triggers so they win.
    def marker(command, arrow):
        shown = ('                        <Setter TargetName="%s" Property="Visibility" Value="Visible"/>\n' % arrow) if arrow else \
                '                        <Setter TargetName="Item" Property="Visibility" Value="Hidden"/>\n'
        return ('                    <DataTrigger Binding="{Binding Command}" Value="%s">\n'
                '                        <Setter TargetName="Item" Property="Height" Value="%d"/>\n'
                '                        <Setter TargetName="Item" Property="Color" Value="{StaticResource A_Color_HUD_ItemBackground}"/>\n'
                '                        <Setter TargetName="Dpad" Property="Height" Value="%d"/>\n'
                '                        <Setter TargetName="Dpad" Property="Visibility" Value="Hidden"/>\n'
                '                        <Setter TargetName="Label" Property="Visibility" Value="Collapsed"/>\n'
                '%s'
                '                    </DataTrigger>\n') % (command, MARKER_HEIGHT, MARKER_HEIGHT, shown)

    sub('''                        <Setter TargetName="Dpad"
                                Property="Visibility"
                                Value="Hidden"/>
                    </DataTrigger>
                </DataTemplate.Triggers>''',
        '''                        <Setter TargetName="Dpad"
                                Property="Visibility"
                                Value="Hidden"/>
                    </DataTrigger>
''' + marker("ANNA_MORE_UP", "MoreUp") + marker("ANNA_MORE_DOWN", "MoreDown") + marker("ANNA_MORE_NONE", None) +
        '''                </DataTemplate.Triggers>''')

    # The game's own box for the list (300 high) is left as it is: 6 rows (234) and the two markers (50) fit in it,
    # and so do the 7 of a list that doesn't scroll (273). A taller box would make the grid it shares with the
    # minimap and the ring taller too, and move them.

    if crlf:
        t = t.replace("\n", "\r\n")
    with open(out_path, "wb") as out:
        out.write(t.encode("utf-8"))
    print("wrote", out_path, len(t), "chars")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    main(sys.argv[1], sys.argv[2])
