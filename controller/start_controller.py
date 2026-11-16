from os_ken import cfg
from os_ken.base import app_manager
from os_ken.controller import ofp_handler
from os_ken.lib import hub
from os_ken.controller import controller

cfg.CONF(args=[
    '--ofp-tcp-listen-port', '6653'
])

manager = app_manager.AppManager.get_instance()

manager.applications_cls['os_ken.controller.ofp_handler'] = manager.load_app('os_ken.controller.ofp_handler')
manager.applications_cls['sdn_controller'] = manager.load_app('sdn_controller')

contexts = manager.create_contexts()
services = manager.instantiate_apps(**contexts)
print("Loaded apps:", list(manager.applications.keys()), flush=True)

ofp_server = controller.OpenFlowController()
server_thread = hub.spawn(ofp_server)

print("SDN controller running on port 6653", flush=True)

try:
    hub.joinall(services + [server_thread])
finally:
    manager.close()
