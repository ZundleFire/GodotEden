def can_build(env, platform):
    return True


def configure(env):
    pass


def get_doc_classes():
    return [
        "EdenPlanetClimateProfile",
        "EdenPlanetGeneratorV1",
        "EdenPlanetGeneratorV2",
        "PlanetTectonics",
    ]


def get_doc_path():
    return "doc_classes"
