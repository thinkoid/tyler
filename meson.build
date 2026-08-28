project('tyler', 'c',
        version : '0.1',
        license : 'WTFPL',
        default_options : ['c_std=c11', 'warning_level=2'])

add_project_arguments('-DWLR_USE_UNSTABLE',
                      '-D_POSIX_C_SOURCE=200809L',
                      language : 'c')

wlroots = dependency('wlroots-0.19')
wayland_server = dependency('wayland-server')

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
           dependencies : [wlroots, wayland_server],
           install : true)
