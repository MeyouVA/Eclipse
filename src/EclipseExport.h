// Eclipse - what each module shows to the others.
//
// Eclipse is built as eight libraries and a small program that starts them:
//   EclipseColour   src/colour    colour management: colour spaces, OpenColorIO configurations, LUTs, ICC profiles
//   EclipseModel    src/model     the scene: layers, drawings, keyframes, and the commands that change them
//                     scene/        what a scene is made of, and reading and writing it
//                     document/     the scene in hand, and every change to it as a step that can be undone
//                     geometry/     bending, morphing, filling and tracing artwork
//   EclipseAudio    src/audio     sound
//                     sound/        reading, mixing and playing it
//                     lipsync/      hearing which mouth shape goes with each frame
//   EclipseFormats  src/formats   other programs' files, read and written without the window
//                     flash/        Flash: .fla documents and .swf movies in, .swf out; Flash video
//                     archive/      zip archives
//   EclipseRender   src/render    making pictures of the scene without the window
//                     effects/      what the effects of a layer do to its picture
//   EclipseActions  src/actions   the menus, toolbars and shortcuts read from `resources`, and the look
//                     manager/      the commands themselves
//                     dialogs/      changing shortcuts and toolbars
//                     theme/        colours and icons
//   EclipseCanvas   src/canvas    the Camera and Drawing views and their tools
//   EclipseUi       src/ui        the window and all its views
//                     window/       the main window, its title bars, the Welcome window
//                     timeline/     the Timeline, the Xsheet, the Function view
//                     panels/       Tool Properties, Layer Properties, Colour, Drawing Substitution, effects, the Library
//                     views/        Node View, Top / Side / Perspective, Lip-Sync, and the smaller views
//                     io/           importing, exporting, printing
//   Eclipse.exe     src/app       starts the window
// A class marked with the macro of its module can be used from the modules built on it.
// (Plain functions and structs are shown by the build itself; the macro is what a class
// with signals and slots, or with a value shared by all its objects, needs as well.)
#pragma once

#include <QtCore/qglobal.h>

#if defined(ECLIPSE_COLOUR_BUILD)
#    define ECLIPSE_COLOUR_API Q_DECL_EXPORT
#else
#    define ECLIPSE_COLOUR_API Q_DECL_IMPORT
#endif

#if defined(ECLIPSE_MODEL_BUILD)
#    define ECLIPSE_MODEL_API Q_DECL_EXPORT
#else
#    define ECLIPSE_MODEL_API Q_DECL_IMPORT
#endif

#if defined(ECLIPSE_AUDIO_BUILD)
#    define ECLIPSE_AUDIO_API Q_DECL_EXPORT
#else
#    define ECLIPSE_AUDIO_API Q_DECL_IMPORT
#endif

#if defined(ECLIPSE_FORMATS_BUILD)
#    define ECLIPSE_FORMATS_API Q_DECL_EXPORT
#else
#    define ECLIPSE_FORMATS_API Q_DECL_IMPORT
#endif

#if defined(ECLIPSE_RENDER_BUILD)
#    define ECLIPSE_RENDER_API Q_DECL_EXPORT
#else
#    define ECLIPSE_RENDER_API Q_DECL_IMPORT
#endif

#if defined(ECLIPSE_CANVAS_BUILD)
#    define ECLIPSE_CANVAS_API Q_DECL_EXPORT
#else
#    define ECLIPSE_CANVAS_API Q_DECL_IMPORT
#endif

#if defined(ECLIPSE_ACTIONS_BUILD)
#    define ECLIPSE_ACTIONS_API Q_DECL_EXPORT
#else
#    define ECLIPSE_ACTIONS_API Q_DECL_IMPORT
#endif

#if defined(ECLIPSE_UI_BUILD)
#    define ECLIPSE_UI_API Q_DECL_EXPORT
#else
#    define ECLIPSE_UI_API Q_DECL_IMPORT
#endif
