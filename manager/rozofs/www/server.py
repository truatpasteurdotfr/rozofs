import subprocess
import time
from flask import Flask, render_template, request, got_request_exception
from flask.ext import restful
from flask_restful.representations.json import output_json
from flask.ext.restful import Resource
from rozofs.core.platform import Platform
from rozofs.core.platform import Role
from rozofs.core.constants import LAYOUT_VALUES

#exportd_host = '192.168.1.220'
#exportd_host = '10.0.2.15'

application = Flask(__name__)
api = restful.Api(application)

STR_ROLES = {"exportd": Role.EXPORTD, "storaged": Role.STORAGED, "rozofsmount": Role.ROZOFSMOUNT}

def _roles_to_strings(roles):
    strs = []
    if roles & Role.EXPORTD == Role.EXPORTD:
        strs.append("exportd")
    if roles & Role.STORAGED == Role.STORAGED:
        strs.append("storaged")
    if roles & Role.ROZOFSMOUNT == Role.ROZOFSMOUNT:
        strs.append("rozofsmount")
    return strs


def _ping(host):
	with open('/dev/null', 'w') as devnull:
		if subprocess.call(['ping', '-c', '1', '-W', '1', host], stdout=devnull, stderr=devnull) is not 0:
			return False
		return True


class Configuration(Resource):
	def get(self, exportd_host):
		platform = Platform(exportd_host)
		# get exportd configuration and nodes statuses
		configurations = platform.get_configurations()
		statuses = platform.get_statuses()
		
		if configurations[exportd_host] is None:
			raise Exception("exportd node is off line.")

		econfig = configurations[exportd_host][Role.EXPORTD]
		
		nodes = {}
		for n, r in platform.list_nodes().items():
			nodes[n] = {}
			nodes[n]['up'] = _ping(n)
			nodes[n]['roles'] = _roles_to_strings(r)
		
		rows = []
		# build the tree grid
		rows.append({
			'id':0, 
			'text': 'RozoFS',
			'state':'opened', 
			'children':[],
			'iconCls':'icon-platform',
			'type':'Platform', 
			'status':'',
			'layout': LAYOUT_VALUES[econfig.layout],
			'nodes': nodes})
		i = 1
		for vid, volume in econfig.volumes.items():
			vstat = econfig.stats.vstats[vid]
			rows[0]['children'].append({
				'id':i, 
				'text': 'volume %d'%vid ,
				'state':'opened', 
				'children':[],
				'iconCls':'icon-volume', 
				'type':'Volume',
				'vid': vid,
				'status':'', 
				'vstat':{'bsize':vstat.bsize,
					'bfree':vstat.bfree, 
					'blocks':vstat.blocks}})
			rows[0]['children'][i-1]['children'].append({
				'id':10000*i, 
				'text': 'storages', 
				'state':'opened', 
				'children':[], 
				'iconCls':'icon-group',
				'type':'Storages', 
				'status':''})
			j = 1
			for cid, cluster in volume.clusters.items():
				cstat = vstat.cstats[cid]
				rows[0]['children'][i-1]['children'][0]['children'].append({
					'id':10000*i+j, 
					'text': 'cluster %d'%cid, 
					'state':'opened', 
					'children':[], 
					'iconCls':'icon-cluster', 
					'type':'Cluster', 
					'cid': cid,
					'status':'', 
					'cstat':{'size':cstat.size, 'free':cstat.free}})
				k = 1
				for sid, storage in cluster.storages.items():
					status = -1
					if statuses[storage] is not None:
						status = 0
						if statuses[storage][Role.STORAGED]:
							status = 1
					sstat = cstat.sstats[sid]
					rows[0]['children'][i-1]['children'][0]['children'][j-1]['children'].append({
						'id':10000*i+1000*j+k, 
						'text': storage, 
						'iconCls':'icon-server', 
						'type':'Storage', 
						'status':status, 
						'role': Role.STORAGED, 
						'sstat':{'size':sstat.size, 'free':sstat.free}})
					k = k + 1
				j = j + 1

			if econfig.exports:
				rows[0]['children'][i-1]['children'].append({
					'id':100000*i, 
					'text': 'exports',
					'state':'opened',
					'children':[],
					'iconCls':'icon-group',
					'type':'Exports', 
					'status':''})
				j = 1
				for eid, export in econfig.exports.items():
					if export.vid == vid:
						estat = econfig.stats.estats[eid]
						rows[0]['children'][i-1]['children'][1]['children'].append({
							'id':100000*i+j, 
							'text': 'export %d'%eid, 
							'state':'opened', 
							'children':[], 
							'iconCls':'icon-export', 
							'type':'Export',
							'eid': eid, 
							'status':'', 
							'estat':{'bsize':estat.bsize, 
								'blocks':estat.blocks, 
								'bfree':estat.bfree, 
								'files':estat.files, 
								'ffree':estat.ffree}})
						k = 1
						for h, c in configurations.items():
							if c is not None and Role.ROZOFSMOUNT in c: 
								for tc in c[Role.ROZOFSMOUNT]:
									if tc.export_path == export.root:
										status = -1
										if statuses[h] is not None:
											status = 0
											if statuses[h][Role.ROZOFSMOUNT]:
												status = 1
										rows[0]['children'][i-1]['children'][1]['children'][j-1]['children'].append({
											'id':100000*i+10000*j+k, 
											'text': h, 
											'iconCls':'icon-server', 
											'role': Role.ROZOFSMOUNT, 
											'type':'Mount', 
											'status':status})
										k = k + 1
					j = j + 1
				
			i = i + 1
			
		return rows

class NodesStart(Resource):
    def post(self, exportd_host):
		platform = Platform(exportd_host)
		args = request.json
		for n in args['nodes']:
			platform.start([n['host']], n['role'])

class NodesStop(Resource):
    def post(self, exportd_host):
		platform = Platform(exportd_host)
		args = request.json 
		for n in args['nodes']:
			platform.stop([n['host']], n['role'])

class AddExport(Resource):
    def post(self, exportd_host):
		platform = Platform(exportd_host)
		args = request.json
		platform.create_export(args['vid'])
		# provide time to reload exportd.
		time.sleep(5)
		
class RemoveExport(Resource):
    def post(self, exportd_host):
		platform = Platform(exportd_host)
		args = request.json
		platform.remove_export([args['eid']])
		
class MountExports(Resource):
    def post(self, exportd_host):
		platform = Platform(exportd_host)
		args = request.json
		platform.mount_export(args['eids'])
		
class UmountExports(Resource):
    def post(self, exportd_host):
		platform = Platform(exportd_host)
		args = request.json
		platform.umount_export(args['eids'])
		
class ExpandVolume(Resource):
    def post(self, exportd_host):
		platform = Platform(exportd_host)
		args = request.json
		platform.add_nodes(args['hosts'], args['vid'])
		# provide time to reload exportd.
		time.sleep(5)
		
class RemoveVolume(Resource):
    def post(self, exportd_host):
		platform = Platform(exportd_host)
		args = request.json
		platform.remove_volume(args['vid'])

@application.errorhandler(Exception)
def exception_handler(e):
  return jsonify(message=e.message), 200

@application.route('/')
def index():
	return render_template('rozo.html')

api.add_resource(Configuration, '/<string:exportd_host>/configuration')
api.add_resource(NodesStart, '/<string:exportd_host>/nodes/start')
api.add_resource(NodesStop, '/<string:exportd_host>/nodes/stop')
api.add_resource(AddExport, '/<string:exportd_host>/exports/add')
api.add_resource(RemoveExport, '/<string:exportd_host>/exports/remove')
api.add_resource(MountExports, '/<string:exportd_host>/exports/mount')
api.add_resource(UmountExports, '/<string:exportd_host>/exports/umount')
api.add_resource(ExpandVolume, '/<string:exportd_host>/volumes/expand')
api.add_resource(RemoveVolume, '/<string:exportd_host>/volumes/remove')
