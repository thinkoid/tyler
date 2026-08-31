project('tyler', 'c',
        version : '0.1',
        license : 'WTFPL',
        default_options : ['c_std=c11', 'warning_level=2'])

add_project_arguments('-DWLR_USE_UNSTABLE',
                      '-D_POSIX_C_SOURCE=200809L',
                      language : 'c')

wlroots = dependency('wlroots-0.19')
wayland_server = dependency('wayland-server')
wayland_client = dependency('wayland-client')
xkbcommon = dependency('xkbcommon')
fcft = dependency('fcft')
pixman = dependency('pixman-1')
libdrm = dependency('libdrm')

# wlroots requires the compositor to rig up the wayland-scanner protocol
# generation itself (see dwl's Makefile for the reference recipe).
wayland_scanner = find_program(
        dependency('wayland-scanner', native : true)
                .get_variable('wayland_scanner'))

protocol_dir = dependency('wayland-protocols').get_variable('pkgdatadir')

xdg_shell_h = custom_target(
        'xdg-shell-protocol.h',
        input : protocol_dir / 'stable/xdg-shell/xdg-shell.xml',
        output : 'xdg-shell-protocol.h',
        command : [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'])

executable('tyler', ['tyler.c', xdg_shell_h],
           dependencies : [wlroots, wayland_server, xkbcommon,
                           fcft, pixman, libdrm],
           install : true)

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
