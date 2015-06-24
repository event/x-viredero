env = Environment(CCFLAGS = '-Werror', LIBS = ['X11', 'Xdamage', 'Xext'])
conf = Configure(env)
if conf.CheckLib('usb-1.0') :
    env.Append(CCFLAGS=' -DWITH_USB=1')
    
env.Program('x-viredero', 'x-viredero.c')
