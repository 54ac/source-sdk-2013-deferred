# 1. vgui_controls
set(VGUI_CONTROLS_SRCS
    vgui_controls/AnalogBar.cpp
    vgui_controls/AnimatingImagePanel.cpp
    vgui_controls/AnimationController.cpp
    vgui_controls/BitmapImagePanel.cpp
    vgui_controls/BuildFactoryHelper.cpp
    vgui_controls/BuildGroup.cpp
    vgui_controls/BuildModeDialog.cpp
    vgui_controls/Button.cpp
    vgui_controls/CheckButton.cpp
    vgui_controls/CheckButtonList.cpp
    vgui_controls/CircularProgressBar.cpp
    vgui_controls/ComboBox.cpp
    vgui_controls/consoledialog.cpp
    vgui_controls/ControllerMap.cpp
    vgui_controls/controls.cpp
    vgui_controls/cvartogglecheckbutton.cpp
    vgui_controls/DirectorySelectDialog.cpp
    vgui_controls/Divider.cpp
    vgui_controls/EditablePanel.cpp
    vgui_controls/ExpandButton.cpp
    vgui_controls/FileOpenDialog.cpp
    vgui_controls/FileOpenStateMachine.cpp
    vgui_controls/FocusNavGroup.cpp
    vgui_controls/Frame.cpp
    vgui_controls/GraphPanel.cpp
    vgui_controls/HTML.cpp
    vgui_controls/Image.cpp
    vgui_controls/ImageList.cpp
    vgui_controls/ImagePanel.cpp
    vgui_controls/InputDialog.cpp
    vgui_controls/KeyBindingHelpDialog.cpp
    vgui_controls/KeyBoardEditorDialog.cpp
    vgui_controls/KeyRepeat.cpp
    vgui_controls/Label.cpp
    vgui_controls/ListPanel.cpp
    vgui_controls/ListViewPanel.cpp
    vgui_controls/Menu.cpp
    vgui_controls/MenuBar.cpp
    vgui_controls/MenuButton.cpp
    vgui_controls/MenuItem.cpp
    vgui_controls/MessageBox.cpp
    vgui_controls/MessageDialog.cpp
    vgui_controls/Panel.cpp
    vgui_controls/PanelListPanel.cpp
    vgui_controls/PerforceFileExplorer.cpp
    vgui_controls/PerforceFileList.cpp
    vgui_controls/perforcefilelistframe.cpp
    vgui_controls/ProgressBar.cpp
    vgui_controls/ProgressBox.cpp
    vgui_controls/PropertyDialog.cpp
    vgui_controls/PropertyPage.cpp
    vgui_controls/PropertySheet.cpp
    vgui_controls/QueryBox.cpp
    vgui_controls/RadioButton.cpp
    vgui_controls/RichText.cpp
    vgui_controls/RotatingProgressBar.cpp
    vgui_controls/savedocumentquery.cpp
    vgui_controls/ScalableImagePanel.cpp
    vgui_controls/ScrollableEditablePanel.cpp
    vgui_controls/ScrollBar.cpp
    vgui_controls/ScrollBarSlider.cpp
    vgui_controls/SectionedListPanel.cpp
    vgui_controls/Slider.cpp
    vgui_controls/Splitter.cpp
    vgui_controls/subrectimage.cpp
    vgui_controls/TextEntry.cpp
    vgui_controls/TextImage.cpp
    vgui_controls/ToggleButton.cpp
    vgui_controls/Tooltip.cpp
    vgui_controls/ToolWindow.cpp
    vgui_controls/TreeView.cpp
    vgui_controls/TreeViewListControl.cpp
    vgui_controls/URLLabel.cpp
    vgui_controls/WizardPanel.cpp
    vgui_controls/WizardSubPanel.cpp
    "${SRCDIR}/public/filesystem_helpers.cpp"
    src/vgui_key_translation.cpp
)

add_library(vgui_controls STATIC ${VGUI_CONTROLS_SRCS})
source_sdk_target(vgui_controls)
set_target_properties(vgui_controls PROPERTIES FOLDER "Libraries")
target_include_directories(vgui_controls PUBLIC
    "${SRCDIR}/public/vgui_controls"
)
target_include_directories(vgui_controls PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/vgui_controls"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${SRCDIR}/thirdparty"
    "${SRCDIR}/thirdparty/protobuf/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/vgui_controls/generated_proto"
)
target_compile_definitions(vgui_controls PRIVATE
    LIBNAME=vgui_controls
)

# 2. matsys_controls
set(MATSYS_CONTROLS_SRCS
    matsys_controls/assetpicker.cpp
    matsys_controls/baseassetpicker.cpp
    matsys_controls/colorpickerpanel.cpp
    matsys_controls/curveeditorpanel.cpp
    matsys_controls/gamefiletreeview.cpp
    matsys_controls/manipulator.cpp
    matsys_controls/matsyscontrols.cpp
    matsys_controls/mdlpanel.cpp
    matsys_controls/mdlpicker.cpp
    matsys_controls/mdlsequencepicker.cpp
    matsys_controls/picker.cpp
    matsys_controls/potterywheelpanel.cpp
    matsys_controls/proceduraltexturepanel.cpp
    matsys_controls/QCGenerator.cpp
    matsys_controls/sequencepicker.cpp
    matsys_controls/tgapreviewpanel.cpp
    matsys_controls/vmtpanel.cpp
    matsys_controls/vmtpicker.cpp
    matsys_controls/vmtpreviewpanel.cpp
    matsys_controls/vtfpicker.cpp
    matsys_controls/vtfpreviewpanel.cpp
)

add_library(matsys_controls STATIC ${MATSYS_CONTROLS_SRCS})
source_sdk_target(matsys_controls)
set_target_properties(matsys_controls PROPERTIES FOLDER "Libraries")
target_include_directories(matsys_controls PUBLIC
    "${SRCDIR}/public/matsys_controls"
)
target_include_directories(matsys_controls PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/matsys_controls"
)
target_compile_definitions(matsys_controls PRIVATE
    LIBNAME=matsys_controls
)
