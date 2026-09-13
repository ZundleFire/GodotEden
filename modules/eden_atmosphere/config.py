def can_build(env, platform):
    # Depends on modules/noise for NoiseTexture3D + FastNoiseLite (the baked cloud field).
    return True


def configure(env):
    pass


def get_doc_classes():
    return [
        "EdenPlanetAtmosphere",
        "EdenCloudShell",
    ]


def get_doc_path():
    return "doc_classes"
