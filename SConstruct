env = Environment(CCFLAGS = '-Werror', LIBS = ['X11', 'Xdamage', 'Xext', 'Xfixes'])
conf = Configure(env)
files = ['x-viredero.c', 'ppm.c', 'net.c']
if conf.CheckLib('usb-1.0') :
    env.Append(CCFLAGS=' -DWITH_USB=1')
    files.append('usb.c')

ut = ARGUMENTS.get('usbtest', 0)
if int(ut) :
    files = ['usb-tst.c', 'usb.c']
    env.Program('usb-tst', files)
else :       
    env.Program('x-viredero', files)
