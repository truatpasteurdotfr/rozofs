//~ 
//~ function nodesNodesTreeOnSelect(node) {
	//~ if ($('#nodesTree').tree('isLeaf', node)) {
		//~ if (node.text == "storaged") {
			//~ host =  $('#nodesTree').tree('getParent', node.target).text;
			//~ $('#detailView').panel('refresh', '/node_storaged_config');
			//~ url = '/node/'+host+'/listen'
			//~ $.get(url, function(data, status) {$('#nodeStoragedListenDataGrid').datagrid('loadData', data);})
			//~ url = '/node/'+host+'/storage'
			//~ $.get(url, function(data, status) {$('#nodeStoragedStorageDataGrid').datagrid('loadData', data);})
		//~ }
		//~ if (node.text == "rozofsmount") {
			//~ host =  $('#nodesTree').tree('getParent', node.target).text;
			//~ $('#detailView').panel('refresh', '/node_rozofsmount_config');
			//~ url = '/node/'+host+'/mount'
			//~ $.get(url, function(data, status) {$('#nodeMountDataGrid').datagrid('loadData', data);})
		//~ }
	//~ }
//~ }    
//~ 
//~ function nodesStart(){
	//~ var args = {};
	//~ args.nodes = [];
	//~ var nodes = $('#nodesTree').tree('getChecked');
	//~ for(var i=0, j=0; i<nodes.length; i++) {
		//~ if ($('#nodesTree').tree('isLeaf', nodes[i].target)) {
			//~ args.nodes[j] = {};
			//~ args.nodes[j].host = ($('#nodesTree').tree('getParent',nodes[i].target)).text;
			//~ args.nodes[j].role = nodes[i].text;
			//~ j++;
		//~ }
	//~ }
	//~ // what the fuck !
	//~ //$.post('/nodes/start', args, function(data, status) {$('#nodesTree').tree('reload');}, 'json');
	//~ $.ajax({
		//~ type: 'POST',
		//~ contentType: 'application/json',
		//~ data: JSON.stringify(args),
		//~ dataType: 'json',
		//~ url: '/nodes/start',
		//~ success: function (e) {
			//~ $('#nodesTree').tree('reload');
		//~ }
	//~ });
//~ }
//~ 
//~ function nodesStop(){
	//~ var args = {};
	//~ args.nodes = [];
	//~ var nodes = $('#nodesTree').tree('getChecked');
	//~ for(var i=0, j=0; i<nodes.length; i++) {
		//~ if ($('#nodesTree').tree('isLeaf', nodes[i].target)) {
			//~ args.nodes[j] = {};
			//~ args.nodes[j].host = ($('#nodesTree').tree('getParent',nodes[i].target)).text;
			//~ args.nodes[j].role = nodes[i].text;
			//~ j++;
		//~ }
	//~ }
	//~ // what the fuck !
	//~ //$.post('/nodes/stop', args, function(data, status) {$('#nodesTree').tree('reload');}, 'json');
	//~ $.ajax({
		//~ type: 'POST',
		//~ contentType: 'application/json',
		//~ data: JSON.stringify(args),
		//~ dataType: 'json',
		//~ url: '/nodes/stop',
		//~ success: function (e) {
			//~ $('#nodesTree').tree('reload');
		//~ }
	//~ });
//~ }
