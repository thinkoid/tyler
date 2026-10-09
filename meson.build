project('tyler', 'c',
        version : '0.1',
        license : 'WTFPL',
        default_options : ['c_std=c11', 'warning_level=2',
                           'force_fallback_for=scenefx'])

add_project_arguments('-DWLR_USE_UNSTABLE',
                      '-D_POSIX_C_SOURCE=200809L',
                      language : 'c')

wlroots = dependency('wlroots-0.20')

# SceneFX replaces wlroots' scene graph and renderer (rounded corners).
# Always the wrap's copy, built statically: it carries the corner patch
# in subprojects/packagefiles, which an installed SceneFX would not.
# The patch: NVIDIA's GLSL ES compiler does not define
# GL_FRAGMENT_PRECISION_HIGH for SceneFX's version-less shaders, so
# every effect shader ran at mediump, a 16-bit float that loses the
# half pixel past 1024 and the odd pixel past 2048: staircased corners
# and 2x2-blocked shadows on a 4K panel. The shaders now declare highp
# outright. With precision restored, the half-pixel fudges SceneFX
# applied to each shape's box come off and every rounded edge is
# anti-aliased half a pixel either side of its line, and the region a
# cut-out excludes from drawing covers the whole corner square, not
# 0.3 of the radius, which left the shadow undrawn at the corner.
# Its wlr_scene_* symbols interpose on wlroots' own: SceneFX
# leaves the surface, xdg-shell and output-layout helpers to wlroots,
# and those call back into the scene core by name.
scenefx = dependency('scenefx-0.5', version : ['>=0.5.0', '<0.6.0'],
                     default_options : ['default_library=static',
                                        'examples=false'])

wayland_server = dependency('wayland-server')
wayland_client = dependency('wayland-client')
xkbcommon = dependency('xkbcommon')
fcft = dependency('fcft')
pixman = dependency('pixman-1')
libdrm = dependency('libdrm')
libinput = dependency('libinput')

# The test harness's input injectors speak protocols wayland-client has
# no headers for; wayland-scanner generates them.
wayland_scanner = find_program(
        dependency('wayland-scanner', native : true)
                .get_variable('wayland_scanner'))

tyler_exe = executable('tyler', 'tyler.c',
                       dependencies : [scenefx, wlroots, wayland_server,
                                       xkbcommon, fcft, pixman, libdrm,
                                       libinput],
                       install : true)

# The status feeder config.h names by PATH; without it an installed
# compositor shows an empty status field.
install_data('tools/tyler-status',
             install_dir : get_option('bindir'),
             install_mode : 'rwxr-xr-x')

# vkbd: the keyboard oracle's key injector, never installed. The
# virtual-keyboard XML is vendored (wlroots ships it, wayland-protocols
# does not).
vk_xml = 'protocol/virtual-keyboard-unstable-v1.xml'

vk_client_h = custom_target(
        'virtual-keyboard-client-header',
        input : vk_xml,
        output : 'virtual-keyboard-unstable-v1-client-protocol.h',
        command : [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'])

vk_glue_c = custom_target(
        'virtual-keyboard-glue',
        input : vk_xml,
        output : 'virtual-keyboard-unstable-v1-protocol.c',
        command : [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'])

executable('vkbd', ['tools/vkbd.c', vk_client_h, vk_glue_c],
           dependencies : [wayland_client, xkbcommon],
           install : false)

vp_xml = 'protocol/wlr-virtual-pointer-unstable-v1.xml'

vp_client_h = custom_target(
        'virtual-pointer-client-header',
        input : vp_xml,
        output : 'wlr-virtual-pointer-unstable-v1-client-protocol.h',
        command : [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'])

vp_glue_c = custom_target(
        'virtual-pointer-glue',
        input : vp_xml,
        output : 'wlr-virtual-pointer-unstable-v1-protocol.c',
        command : [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'])

executable('vptr', ['tools/vptr.c', vp_client_h, vp_glue_c],
           dependencies : [wayland_client],
           install : false)

# Oracles. Headless, so they need no display, no GPU and no seat, and
# run anywhere the compositor builds.
#
# Two outputs is not redundant with one: the single-output quit is clean
# even with the fini() ordering bug present, because the last output
# leaves a null survivor and arrange() returns before it can draw. One
# alone would be a gate that never bites.
teardown_oracle = files('tests/teardown-oracle.sh')

foreach n : ['1', '2', '3']
        test('teardown-' + n + '-outputs',
             teardown_oracle,
             args : [tyler_exe, n],
             timeout : 60)
endforeach
