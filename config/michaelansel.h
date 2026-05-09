#pragma once

// Layers
#define BASE         0
#define NUM          1
#define MAGIC        2
#define FACTORY_TEST 3
#define NAV          4
#define SYM          5
#define UNI          6
#define WIN          7

// Hammerspoon Hyper-key triggers (HYPER is defined in glove80.keymap)
#define HS_WARP HYPER(A) // Launch warpd
#define HS_CHRM HYPER(B) // Launch Chrome
#define HS_MEET HYPER(C) // Launch Meet
#define HS_TYPR HYPER(D) // Launch Typora
#define HS_SLAK HYPER(E) // Launch Slack
#define HS_CODE HYPER(F) // Launch VS Code
#define HS_PTAB HYPER(G) // Previous Tab
#define HS_NTAB HYPER(H) // Next Tab
#define HS_SCRN HYPER(I) // Switch Monitor Focus
#define HS_TERM HYPER(J) // Launch iTerm2
#define HS_RTM  HYPER(K) // Launch Remember the Milk
#define HS_SPFY HYPER(L) // Launch Spotify
#define HS_SCDN HYPER(M) // Scroll Down
#define HS_SCUP HYPER(N) // Scroll Up
#define HS_BTN1 HYPER(O) // Click Mouse Button 1
#define HS_MCTR HYPER(P) // Move mouse to center of screen
#define HS_MENU HYPER(Q) // Open interactive hammerspoon menu system
#define LEADERK HYPER(S) // Open Leader Key

// Magnet.app
#define MAG_L   LC(LA(LEFT))
#define MAG_R   LC(LA(RIGHT))
#define MAG_MAX LC(LA(ENTER))
#define MAG_RST LC(LA(BSPC))

// Aerospace.app
#define AE_EQLS LC(LA(EQUAL))        // balance-sizes
#define AE_FULL LC(LA(LS(EQUAL)))    // fullscreen
#define AE_FO_D LC(LA(J))            // focus down
#define AE_FO_L LC(LA(H))            // focus left
#define AE_FO_R LC(LA(L))            // focus right
#define AE_FO_U LC(LA(K))            // focus up
#define AE_JO_D LC(LA(LG(J)))        // join-with down
#define AE_JO_L LC(LA(LG(H)))        // join-with left
#define AE_JO_R LC(LA(LG(L)))        // join-with right
#define AE_JO_U LC(LA(LG(K)))        // join-with up
#define AE_MV_D LC(LA(LS(J)))        // move down
#define AE_MV_L LC(LA(LS(H)))        // move left
#define AE_MV_R LC(LA(LS(L)))        // move right
#define AE_MV_U LC(LA(LS(K)))        // move up
#define AE_TGAC LC(LA(COMMA))        // layout tiles accordion
#define AE_TGFT LC(LA(DOT))          // layout floating tiling
#define AE_TGRT LC(LA(FSLH))         // layout horizontal vertical
#define AE_TRST LC(LA(SQT))          // flatten-workspace-tree
#define AE_FMON lk_aero_foc_mon      // focus-monitor --wrap-around next (leader key macro)

// Leader Key sequences
// X(key_label, sequence_string)
#define LEADER_KEY_APP_SEQUENCES(X) \
    X(lk_aero_foc_mon, A F) /* Aerospace Focus Next Monitor */ \
    X(lk_open_typora,  O N) /* Open Typora */
