var exportd = '';


function login() {
	exportd = $('#exportd').val();
	$('#platformTree').treegrid({url:exportd+'/configuration'});
	$('#platformTree').treegrid('reload');
	$('#login').hide();
}

function platformTreeNodesToStartStopArgs() {
	var args = {};
	args.nodes = [];
	selected = $('#platformTree').treegrid('getSelected');
	if (selected.type == 'Storage' || selected.type == 'Mount') {
		args.nodes[0] = {}
		args.nodes[0].host = selected.text;
		args.nodes[0].role = selected.role;
	} else {
		children = $('#platformTree').treegrid('getChildren', selected.id);
		for (var i=0, j=0; i < children.length; i++) {
			n = children[i];
			if (n.type == 'Storage' || n.type == 'Mount') {
				args.nodes[j] = {}
				args.nodes[j].host = n.text;
				args.nodes[j].role = n.role;
				j++;
			}
		}
	}
	return args;
}

function nodesStart(){
	// what the fuck !
	//$.post('/nodes/start', args, function(data, status) {$('#nodesTree').tree('reload');}, 'json');
	//$.messager.progress({title:'Starting Nodes', msg:'Processing, Please Wait'});
	showLoader();
	var args = platformTreeNodesToStartStopArgs();
	$.ajax({
		type: 'POST',
		contentType: 'application/json',
		data: JSON.stringify(args),
		dataType: 'json',
		url: '/'+exportd+'/nodes/start',
		success: function(e) {
			hideLoader();
			$('#platformTree').treegrid('reload');
		},
		error:  function (e) {
			hideLoader();
			showError(e);
		}
	});
}

function nodesStop(){
	// what the fuck !
	//$.post('/nodes/stop', args, function(data, status) {$('#nodesTree').tree('reload');}, 'json');
	//$.messager.progress({title:'Stopping Nodes', msg:'Processing, Please Wait'});
	showLoader();
	var args = platformTreeNodesToStartStopArgs();
	$.ajax({
		type: 'POST',
		contentType: 'application/json',
		data: JSON.stringify(args),
		dataType: 'json',
		url: '/'+exportd+'/nodes/stop',
		success: function (e) {
			hideLoader();
			$('#platformTree').treegrid('reload');
		},
		error:  function (e) {
			hideLoader();
			showError(e);
		}
	});
}

function platformTreeNodesToAddExportArgs() {
	selected = $('#platformTree').treegrid('getSelected');
	parent = $('#platformTree').treegrid('getParent', selected.id);
	return {'vid': parent.vid};
}

function addExport() {
	var args = platformTreeNodesToAddExportArgs();
	showLoader();
	$.ajax({
		type: 'POST',
		contentType: 'application/json',
		data: JSON.stringify(args),
		dataType: 'json',
		url: '/exports/add',
		success: function (e) {
			hideLoader();
			$('#platformTree').treegrid('reload');
		},
		error:  function (e) {
			hideLoader();
			showError(e);
		}
	});
}

function platformTreeNodesToRemoveExportArgs() {
	selected = $('#platformTree').treegrid('getSelected');
	return {'eid': selected.eid};
}

function removeExport() {
	var args = platformTreeNodesToRemoveExportArgs();
	showLoader();
	$.ajax({
		type: 'POST',
		contentType: 'application/json',
		data: JSON.stringify(args),
		dataType: 'json',
		url: '/'+exportd+'/exports/remove',
		success: function (e) {
			hideLoader();
			$('#platformTree').treegrid('reload');
		},
		error:  function (e) {
			hideLoader();
			showError(e);
		}
	});
}

function platformTreeNodesToMountExportsArgs() {
	args = {'eids': []};
	selected = $('#platformTree').treegrid('getSelected');
	if (selected.type == "Export") {
		args.eids[0] = selected.eid;
	} else if (selected.type == "Exports") {
		var children = $('#platformTree').treegrid('getChildren', selected.id);
		var j = 0;
		for (var i=0; i < children.length; i++) {
			var n = children[i];
			if (n.type == "Export") {
				args.eids[j++] = n.eid;
			}
		}
	}
	return args;
}

function mountExports() {
	var args = platformTreeNodesToMountExportsArgs();
	showLoader();
	$.ajax({
		type: 'POST',
		contentType: 'application/json',
		data: JSON.stringify(args),
		dataType: 'json',
		url: '/'+exportd+'/exports/mount',
		success: function (e) {
			hideLoader();
			$('#platformTree').treegrid('reload');
		},
		error:  function (e) {
			hideLoader();
			showError(e);
		}
	});
}

function umountExports() {
	var args = platformTreeNodesToMountExportsArgs();
	showLoader();
	$.ajax({
		type: 'POST',
		contentType: 'application/json',
		data: JSON.stringify(args),
		dataType: 'json',
		url: '/'+exportd+'/exports/umount',
		success: function (e) {
			hideLoader();
			$('#platformTree').treegrid('reload');
		},
		error:  function (e) {
			hideLoader();
			showError(e);
		}
	});
}

function expandVolume() {
	var i = 0;
	var args = {'vid': null, 'hosts': []};
	var selected = $('#platformTree').treegrid('getSelected');
	if (selected.type == "Volume") {
		args.vid = selected.vid;
	}
	var safe = $('#platformTree').treegrid('find', 0).layout[2];
	
	$('#detailView').empty();
	$('#detailView').panel('setTitle', 'Expand Volume');
	$('#detailView').append('<div id="cdiv" class="detail-content">');
	$('#cdiv').append('<div id="hdiv">');
	$('#hdiv').append('<label style="margin-right:5px;">Host:</label>');
	$('#hdiv').append('<input id="ii" class="easyui-validatebox" type="text" data-options="required:true" />');
	$('#hdiv').append('<a id="btnSubmit" href="javascript:void(0)" class="easyui-linkbutton" style="margin:5px;">Add</a>');
	$('#btnSubmit').linkbutton({iconCls: 'icon-add'});
    $('#hdiv').append('<a id="btnExpand" href="javascript:void(0)" class="easyui-linkbutton" style="margin:5px;">Expand</a><br/>');
	$('#btnExpand').linkbutton({iconCls: 'icon-ok'});
	$('#btnExpand').linkbutton('disable');
	$('#cdiv').append('</div>');
	$('#detailView').append('</div>');
	$(function(){
		$('#btnSubmit').bind('click', function() {
			args['hosts'][i] = $('#ii').val();
			$('#cdiv').append('<div id="ndiv'+i+'" style="float:left;padding:20px;">');
			$('#ndiv'+i).append('<img src="/static/node.png"/></br>');
			$('#ndiv'+i).append($('#ii').val());
			$('#cdiv').append('</div>');
			$('#ii').val('');
			if (++i >= safe) {
				$('#btnExpand').linkbutton('enable');
			}
		});
    });
    
    $(function(){
		$('#btnExpand').bind('click', function(){
			showLoader();
			$.ajax({
				type: 'POST',
				contentType: 'application/json',
				data: JSON.stringify(args),
				dataType: 'json',
				url: '/'+exportd+'/volumes/expand',
				success: function (e) {
					$('#detailView').empty();
					hideLoader();
					$('#platformTree').treegrid('reload');
				},
				error:  function (e) {
					$('#detailView').empty();
					hideLoader();
					showError(e);
				}
			});
		});
    });
}

function platformTreeNodesToRemoveVolumeArgs() {
	selected = $('#platformTree').treegrid('getSelected');
	return {'vid': selected.vid};
}

function removeVolume() {
	var args = platformTreeNodesToRemoveVolumeArgs();
	showLoader();
	$.ajax({
		type: 'POST',
		contentType: 'application/json',
		data: JSON.stringify(args),
		dataType: 'json',
		url: '/'+exportd+'/volumes/remove',
		success: function (e) {
			hideLoader();
			$('#platformTree').treegrid('reload');
		},
		error:  function (e) {
			hideLoader();
			showError(e);
		}
	});
}

function showError(e) {
	$.messager.alert('Error', e.message, 'error');
}

function showLoader() {
	$('#spinner').show();
}

function hideLoader() {
	$('#spinner').hide();
}

function platformTreeStatusFormatter(value) {
	switch(value) {
		case -1: return "<img src='static/red-bullet.png'/>";
		case 0: return "<img src='static/yellow-bullet.png'/>";
		case 1: return "<img src='static/green-bullet.png'/>";
		default: return "<span>"+value+"</span>";
	}
}

function platformTreeOnContextMenu(e, row) {
	e.preventDefault();
	$('#platformTree').treegrid('select', row.id);
	var node = $('#platformTree').treegrid('getSelected');
	$('#platformTree'+node.type+'ContextMenu').menu('show', {left: e.pageX, top: e.pageY});
}

function platformTreePlatformPopulateDetailView() {
	var nodes = $('#platformTree').treegrid('getSelected')['nodes'];
	$('#detailView').panel('setTitle', 'Platform Overview');
	$('#detailView').append('<div id="pdiv" class="detail-content">');
	var i=0;
	for (var key in nodes) {
		n = nodes[key];
		$('#pdiv').append('<div id="ndiv'+i+'" style="float:left;padding:20px;">');
		$('#ndiv'+i).append('<img src="/static/node.png"/></br>');
		if (n['up'] == true) {
			$('#ndiv'+i).append('<span style="color:#90EE90;">'+key+'</span>');
		} else {
			$('#ndiv'+i).append('<span style="color:#F08080;">'+key+'</span>');
		} 
		for (var j=0; j < n['roles'].length; j++) {
			$('#ndiv'+i).append('<br>');
			$('#ndiv'+i).append(n['roles'][j]);
		}
		$('#pdiv').append('</div>');
		i++;
	}
	$('#detailView').append('</div>');
}

function platformTreeVolumePopulateDetailView() {
	var node = $('#platformTree').treegrid('getSelected');
	var vstat = node['vstat'];
	var capacity = vstat.blocks*vstat.bsize/(1024*1024*1024);
	var free = vstat.bfree*vstat.bsize/(1024*1024*1024);
	var used = capacity - free;
	
	var volumeData = [
			{name: "Free", y: Number(free.toFixed(2)), color: "#90EE90"},
			{name: "Used", y: Number(used.toFixed(2)), color: "#F08080"}
		];
    var clusterData = [];
    var children = $('#platformTree').treegrid('getChildren', node.id);
    // ugly ! but we need to know number of clusters
    var nbClusters = 0;
    for (var i=0; i < children.length; i++) {
		if (children[i].type == 'Cluster') {
			nbClusters++;
		}
	}
	for (var i=0, j=0; i < children.length; i++) {
		var n = children[i];
		if (n.type == 'Cluster') {
			var cstat = n['cstat'];
			var nsize = cstat.size/(1024*1024*1024);
			var nfree = cstat.free/(1024*1024*1024);
			var nused = nsize - nfree;
			clusterData[j] = {name: n.text, y: Number(nfree.toFixed(2)), color: Highcharts.Color("#90EE90").brighten((0.02)*(i+1)).get()};
			clusterData[j+nbClusters] = {name: n.text, y: Number(nused.toFixed(2)), color: Highcharts.Color("#F08080").brighten((0.02)*(i+1)).get()};
			j++;
		}
	}

	$('#detailView').panel('setTitle', 'Cluster id: ' + node['cid']);
	$('#detailView').append('<div id="cdiv" class="detail-content">');
	$('#detailView').append('</div>');
    
	// Create the chart
    $('#cdiv').highcharts({
		chart: {type: 'pie'},
        title: {text: 'Volume Usage'},
        credits: {enabled: false},
        yAxis: {title: {text: 'Total volume usage'}},
        plotOptions: {pie: {shadow: true,center: ['50%', '50%']}},
        tooltip: {valueSuffix: 'GB'},
        series: [{
				name: 'Capacity',
				data: volumeData,
				size: '60%',
				dataLabels: {
					formatter: function() {
						return '<b>'+ this.point.name +': '+ this.y +'GB</b>'
					},
					color: 'white',
					distance: -30
				}
            }, {
                name: 'Capacity',
                data: clusterData,
                size: '80%',
                innerSize: '60%',
                dataLabels: {
                    formatter: function() {
						return '<b>'+ this.point.name +': '+ this.y +'GB</b>'
                    }
                }
            }]
        });
}

function platformTreeClusterPopulateDetailView() {
	var node = $('#platformTree').treegrid('getSelected');
	var cstat = node['cstat'];
	var size = cstat.size/(1024*1024*1024);
	var free = cstat.free/(1024*1024*1024);
	var used = size - free;
	
	var clusterData = [
			{name: "Free", y: Number(free.toFixed(2)), color: "#90EE90"},
			{name: "Used", y: Number(used.toFixed(2)), color: "#F08080"}
		];
    var nodesData = [];
    var children = $('#platformTree').treegrid('getChildren', node.id);
	for (var i=0; i < children.length; i++) {
		var n = children[i];
		var sstat = n['sstat'];
		var nsize = sstat.size/(1024*1024*1024);
		var nfree = sstat.free/(1024*1024*1024);
		var nused = nsize - nfree;
		nodesData[i] = {name: n.text, y: Number(nfree.toFixed(2)), color: Highcharts.Color("#90EE90").brighten((0.02)*(i+1)).get()};
		nodesData[i+children.length] = {name: n.text, y: Number(nused.toFixed(2)), color: Highcharts.Color("#F08080").brighten((0.02)*(i+1)).get()};
	}

	$('#detailView').panel('setTitle', 'Cluster id: ' + node['cid']);
	$('#detailView').append('<div id="cdiv" class="detail-content">');
	$('#detailView').append('</div>');
    
	// Create the chart
    $('#cdiv').highcharts({
		chart: {type: 'pie'},
		credits: {enabled: false},
        title: {text: 'Cluster Usage'},
		yAxis: {title: {text: 'Total volume usage'}},
		plotOptions: {pie: {shadow: true, center: ['50%', '50%']}},
        tooltip: {valueSuffix: 'GB'},
        series: [{
                name: 'Capacity',
                data: clusterData,
                size: '60%',
                dataLabels: {
                    formatter: function() {
						return '<b>'+ this.point.name +': '+ this.y +'GB</b>'
                    },
                    color: 'white',
                    distance: -30
                }
            }, {
                name: 'Capacity',
                data: nodesData,
                size: '80%',
                innerSize: '60%',
                dataLabels: {
                    formatter: function() {
						return '<b>'+ this.point.name +': '+ this.y +'GB</b>'
                    }
                }
            }]
        });
}

function platformTreeExportPopulateDetailView() {
	var node = $('#platformTree').treegrid('getSelected');
	var estat = node['estat'];
	var capacity = estat.blocks*estat.bsize/(1024*1024*1024);
	var free = estat.bfree*estat.bsize/(1024*1024*1024);
	var used = capacity - free;
	var capacityData = [
			{name: "Free", y: Number(free.toFixed(2)), color: "#90EE90"},
			{name: "Used", y: Number(used.toFixed(2)), color: "#F08080"}
		];
	var files = estat.files;
	var ffree = estat.ffree;
	var filesData = [
			{name: "Free", y: Number(ffree.toFixed(2)), color: "#90EE90"},
			{name: "Used", y: Number(files.toFixed(2)), color: "#F08080"}
		];
	
	$('#detailView').panel('setTitle', 'Export id: ' + node['eid']);
	$('#detailView').append('<div id="content" class="detail-content">');
	$('#content').append('<div id="ldiv" class="detail-left"></div><div id="rdiv" class="detail-right"></div>');
	$('#detailView').append('</div>');
	// Create the charts
    $('#ldiv').highcharts({
		chart: {type: 'pie'},
        title: {text: 'Export Capacity Usage'},
        yAxis: {title: {text: 'Total export usage'}},
        plotOptions: {pie: {shadow: true, center: ['50%', '50%']}},
		tooltip: {valueSuffix: 'GB'},
        series: [{
                name: 'Capacity',
                data: capacityData,
                size: '80%',
                dataLabels: {
                    formatter: function() {
						return '<b>'+ this.point.name +': '+ this.y +'GB</b>'
                    },
                    color: 'white',
                    distance: -30
                }
            }]
        });
	$('#rdiv').highcharts({
		chart: {type: 'pie'},
        title: {text: 'Export Files Usage'},
        yAxis: {title: {text: 'Total export usage'}},
        plotOptions: {pie: {shadow: true, center: ['50%', '50%']}},
        series: [{
                name: 'Files',
                data: filesData,
                size: '80%',
                dataLabels: {
                    formatter: function() {
						return '<b>'+ this.point.name +': '+ this.y +'</b>'
                    },
                    color: 'white',
                    distance: -30
                }
            }]
        });
}

function platformTreeOnClickRow(row) {
	$('#detailView').empty();
	var node = $('#platformTree').treegrid('getSelected');
	if (node.type == 'Platform') {
		platformTreePlatformPopulateDetailView();
	} else if (node.type == 'Volume') {
		platformTreeVolumePopulateDetailView();
	} else if (node.type == 'Cluster') {
		platformTreeClusterPopulateDetailView();
	} else if (node.type == 'Export') {
		platformTreeExportPopulateDetailView();
	}
}
