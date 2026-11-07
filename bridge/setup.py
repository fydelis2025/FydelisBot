from setuptools import setup

package_name = "fydelis_bridge"

setup(
    name=package_name,
    version="2.0.0",
    packages=[package_name],
    data_files=[
        ("share/ament_index/resource_index/packages", ["resource/" + package_name]),
        ("share/" + package_name, ["package.xml"]),
        ("share/" + package_name + "/config", ["config/bridge.yaml"]),
    ],
    install_requires=["setuptools"],
    zip_safe=True,
    maintainer="Fydelis",
    maintainer_email="fydelistech@gmail.com",
    description="Ponte TCP → /joint_states com watchdog e E-STOP (FydelisBot).",
    license="Apache-2.0",
    entry_points={
        "console_scripts": [
            "fydelis_bridge = fydelis_bridge.node:main",
        ],
    },
)

