// Orkige Player - the Android entry Activity.
//
// SDL3's Android application model: SDLActivity loads the native libraries,
// creates the SDL surface and calls the SDL_main inside libmain.so (the
// orkige_player target built by the android-debug CMake preset - SDL3, OGRE
// and the engine are all statically linked into that one lib).
//
// The editor's play mode starts this activity with intent extras:
//   am start -n com.orkitec.orkigeplayer/.OrkigeActivity \
//       --es scene <path> --ei debugPort <port>
// which surface here as the SDL_main argv (a relative scene path is resolved
// against the app files dir by the player, see tools/player/main.cpp).
package com.orkitec.orkigeplayer;

import org.libsdl.app.SDLActivity;

public class OrkigeActivity extends SDLActivity {

    @Override
    protected String[] getLibraries() {
        // just libmain.so - SDL3 is statically linked into it (the SDL Java
        // glue binds through the JNI_OnLoad/RegisterNatives inside it)
        return new String[] { "main" };
    }

    @Override
    protected String[] getArguments() {
        String scene = getIntent().getStringExtra("scene");
        int debugPort = getIntent().getIntExtra("debugPort", 0);
        // the project manifest's export.orientation, delivered explicitly:
        // a play session pushes only the temp scene to the device, so the
        // player cannot read the setting from the manifest itself
        String orientation = getIntent().getStringExtra("orientation");
        java.util.ArrayList<String> args = new java.util.ArrayList<String>();
        if (scene != null && !scene.isEmpty()) {
            args.add(scene);
            if (debugPort > 0) {
                args.add("--debug-port");
                args.add(String.valueOf(debugPort));
            }
        }
        // launcher start with no extras: the player falls back to the
        // bundled example scene
        if (orientation != null && !orientation.isEmpty()) {
            args.add("--orientation");
            args.add(orientation);
        }
        return args.toArray(new String[0]);
    }
}
