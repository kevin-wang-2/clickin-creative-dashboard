#include <gtest/gtest.h>
#include "providers/core_asset/CoreAssetPlugin.h"
#include "providers/local_file/LocalFilePlugin.h"
#include "providers/local_audio/LocalAudioPlugin.h"

TEST(PluginManifest, CoreAsset) {
    clickin::CoreAssetPlugin p;
    auto m = p.manifest();
    EXPECT_EQ(m.pluginId, "builtin.core_asset");
    EXPECT_TRUE(m.builtin);
    EXPECT_TRUE(m.critical);
}

TEST(PluginManifest, LocalFile) {
    clickin::LocalFilePlugin p;
    EXPECT_EQ(p.manifest().pluginId, "builtin.local_file");
}

TEST(PluginManifest, LocalAudio) {
    clickin::LocalAudioPlugin p;
    EXPECT_EQ(p.manifest().pluginId, "builtin.local_audio");
}
