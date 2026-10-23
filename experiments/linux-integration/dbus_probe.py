#!/usr/bin/python3
"""Private session contract smoke; mock watcher is deliberately not a desktop tray."""
import json, os, subprocess, sys, time
import dbus, dbus.service, dbus.mainloop.glib
from gi.repository import GLib
dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
bus = dbus.SessionBus()
class Watcher(dbus.service.Object):
    @dbus.service.method('org.kde.StatusNotifierWatcher', in_signature='s', out_signature='')
    def RegisterStatusNotifierItem(self, service):
        assert service == 'org.kde.StatusNotifierItem.Cliptiq'
        open(sys.argv[2], 'w').write(str(service))
class Item(dbus.service.Object):
    @dbus.service.method('org.kde.StatusNotifierItem', in_signature='ii', out_signature='')
    def Activate(self, x, y):
        open(sys.argv[2]+'.activate', 'w').write(f'{x},{y}')
    @dbus.service.method('org.freedesktop.DBus.Properties', in_signature='ss', out_signature='v')
    def Get(self, interface, prop):
        return {'Menu': dbus.ObjectPath('/MenuBar'), 'Id': 'cliptiq-spike', 'Status': 'Active', 'Category': 'ApplicationStatus'}[prop]
class Menu(dbus.service.Object):
    @dbus.service.method('com.canonical.dbusmenu', in_signature='iias', out_signature='u(ia{sv}av)')
    def GetLayout(self, parent, depth, names):
        properties=dbus.Dictionary({'label': dbus.String('Open Cliptiq'), 'enabled': dbus.Boolean(True)}, signature='sv')
        child=dbus.Struct((dbus.Int32(1), properties, dbus.Array([], signature='v')), signature='ia{sv}av', variant_level=1)
        return (dbus.UInt32(1), (dbus.Int32(0), dbus.Dictionary({},signature='sv'), dbus.Array([child], signature='v')))
    @dbus.service.method('com.canonical.dbusmenu', in_signature='isvu', out_signature='')
    def Event(self, item, event, data, timestamp):
        assert item == 1 and event == 'clicked'
        open(sys.argv[2]+'.event', 'w').write('clicked')
if len(sys.argv)>1:
    if sys.argv[1]=='watcher':
        name=dbus.service.BusName('org.kde.StatusNotifierWatcher',bus); obj=Watcher(bus,'/StatusNotifierWatcher')
    else:
        name=dbus.service.BusName('org.kde.StatusNotifierItem.Cliptiq',bus); obj=Item(bus,'/StatusNotifierItem'); menu=Menu(bus,'/MenuBar')
    GLib.MainLoop().run()
else:
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        marker=tmp+'/registered'
        children=[subprocess.Popen([sys.executable,__file__,mode,marker]) for mode in ('watcher','item')]
        try:
            for _ in range(100):
                if all(bus.name_has_owner(n) for n in ('org.kde.StatusNotifierWatcher','org.kde.StatusNotifierItem.Cliptiq')): break
                time.sleep(.02)
            watcher=dbus.Interface(bus.get_object('org.kde.StatusNotifierWatcher','/StatusNotifierWatcher'),'org.kde.StatusNotifierWatcher')
            watcher.RegisterStatusNotifierItem('org.kde.StatusNotifierItem.Cliptiq')
            item=bus.get_object('org.kde.StatusNotifierItem.Cliptiq','/StatusNotifierItem')
            path=dbus.Interface(item,'org.freedesktop.DBus.Properties').Get('org.kde.StatusNotifierItem','Menu')
            layout=dbus.Interface(bus.get_object('org.kde.StatusNotifierItem.Cliptiq',path),'com.canonical.dbusmenu')
            revision,root=layout.GetLayout(0,-1,[])
            assert revision==1 and root[2][0][1]['label']=='Open Cliptiq'
            layout.Event(1,'clicked',dbus.Int32(0),0)
            dbus.Interface(item,'org.kde.StatusNotifierItem').Activate(12,34)
            assert open(marker).read()=='org.kde.StatusNotifierItem.Cliptiq'
            assert open(marker+'.event').read()=='clicked' and open(marker+'.activate').read()=='12,34'
            activatable=bus.list_activatable_names()
            portal={'name_owner':bool(bus.name_has_owner('org.freedesktop.portal.Desktop')), 'activatable': 'org.freedesktop.portal.Desktop' in activatable}
            if portal['activatable'] and not portal['name_owner']:
                try:
                    dbus.Interface(bus.get_object('org.freedesktop.DBus','/org/freedesktop/DBus'),'org.freedesktop.DBus').StartServiceByName('org.freedesktop.portal.Desktop',dbus.UInt32(0),timeout=5)
                    portal['name_owner']=bool(bus.name_has_owner('org.freedesktop.portal.Desktop'))
                except dbus.DBusException as error:
                    portal['activation_error']=str(error)
            if portal['name_owner']:
                xml=dbus.Interface(bus.get_object('org.freedesktop.portal.Desktop','/org/freedesktop/portal/desktop'),'org.freedesktop.DBus.Introspectable').Introspect()
                if os.environ.get('CLIPTIQ_RESULT_PATH'):
                    open(os.environ['CLIPTIQ_RESULT_PATH']+'.portal.xml','w').write(xml)
                portal.update({n:n in xml for n in ('GlobalShortcuts','Clipboard')})
            else:
                portal.update({'GlobalShortcuts':None,'Clipboard':None,'reason':'No portal service owner in private session; interfaces unavailable to introspect'})
            result=json.dumps({'mock_sni_registration':True,'menu_layout':True,'menu_click':True,'activate':True,'portal':portal})
            if os.environ.get('CLIPTIQ_RESULT_PATH'):
                open(os.environ['CLIPTIQ_RESULT_PATH'],'w').write(result+'\n')
            else:
                print(result)
        finally:
            for child in children: child.terminate(); child.wait(timeout=5)
