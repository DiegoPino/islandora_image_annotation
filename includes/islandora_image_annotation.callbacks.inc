<?php

/**
 * Ajax callback for Shared Canvas
 * Supplies the basic data, including maifest uri
 * @global url $base_url
 * @param string $pid
 */
function islandora_canvas_setup($pid) {
  global $base_url;
  global $user;

  module_load_include('inc', 'islandora_image_annotation', 'includes/islandora_rdf_builder');
  //$books = get_collection_from_pid($pid);
  $book = $pid; //$books[0];
  $item = islandora_object_load($pid);
  $title = $item->label;
  $pages = array();
  $pages[1] = $pid;
  $position = array_search($pid, $pages);
  $results = array();
  $results['manifest_url'] = "$base_url/emic/serve/$pid/Manifest/manifest.xml";
  $results['page_title'] = $item->label;
  $results['object_base'] = "$base_url/islandora/object/$pid";
  $results['islandora_post_url'] = "$base_url/emic/shared/add_annotation/$pid";
  $results['islandora_get_annotation'] = "$base_url/emic/get_annotation/";
  $results['islandora_delete_annotation'] = "$base_url/emic/delete_annotation/$pid/";
  $results['get_annotation_list_url'] = "$base_url/emic/get_urns/$pid";
  $results['islandora_update_annotation'] = "$base_url/emic/update_annotation/";
  $results['position'] = $position;
  $results['pages'] = $pages;
  $results['no_edit'] = !user_access(ISLANDORA_IMAGE_ANNOTATION_CREATE);
  $json = json_encode($results);
  echo $json;
}

/**
 * Creates annotation objects from html supplied by Shared Canvas
 * @global url $base_url
 * @param string $pid
 */
function islandora_add_annotation($pid) {
  global $user;
  global $base_url;
  //module_load_include('inc', 'fedora_repository', 'api/fedora_item');
  module_load_include('inc', 'emic_canvas', 'islandora_rdf_builder');
  module_load_include('inc', 'emic_canvas', 'build_sharedcanvas_streams');
  module_load_include('inc', 'islandora', 'includes/islandora.ingest');
  $annotation_xml = emic_shared_transform_annotationList(urldecode($_POST['data']), 'html');
  $type = $_POST['type'];
  if (!$type) {
    $type = 'unclassified';
  }
  $xml = simplexml_load_string($annotation_xml);
  $labels = $xml->xpath('//dc:title');
  $annotations = $xml->xpath('//rdf:type[@rdf:resource="http://www.openannotation.org/ns/Annotation"]');
  $parents = $annotations[0]->xpath("parent::*");
  $parent = $parents[0];
  foreach ($parent->attributes('rdf', TRUE) as $key => $value) {
    $urn = (string) $value;
  }
  $namespace = substr($pid, 0, strpos($pid, ":"));
  $item = islandora_ingest_get_object('islandora:OACCModel', $pid, 'isMemberOf', $namespace);
  $item->label = (string)$labels[0];
  //relationships->add(FEDORA_MODEL_URI, 'hasModel', 'islandora:imageCModel');
  $item->relationships->add(FEDORA_MODEL_URI, 'hasModel', 'islandora:OACCModel');
  $item->relationships->add(ISLANDORA_RELS_EXT_URI, 'targetedBy', $user->name);
  $item->relationships->add(ISLANDORA_RELS_EXT_URI, 'targets', $pid);
  $item->relationships->add('http://islandora.ca/ontology/relsext#', 'isAnnotationType', $type, TRUE);
  $item->relationships->add(ISLANDORA_RELS_EXT_URI,'hasURN', $urn, TRUE);
  //$item->add_datastream_from_string($annotation_xml, 'OAC', 'OAC', 'text/xml', 'X');
  $new_ds = $item->constructDatastream('OAC', 'X');
  $new_ds->label = 'OAC';
  $new_ds->mimetype = 'text/xml';
  $new_ds->content = $annotation_xml;
  $item->ingestDatastream($new_ds);
  islandora_ingest_add_object($item);
  sleep(1);
  foreach ($parent->attributes('rdf', TRUE) as $key => $value) {
    echo (string) $value;
  }
}

/**
 * transforms input from SharedCanvas to AnnotationList
 * @param string $xml_in
 * @return string
 */
function emic_shared_transform_annotationList($xml_in, $input_type) {

  if ($xml_in == NULL || strlen($xml_in) < 5) {
    return " ";
  }

  $path = drupal_get_path('module', 'islandora_image_annotation') . '/xslt/';
  $xslts = array(
    'html' => $path . 'html_to_AnnotationList.xsl',
    'rdf' => $path . 'AnnotationList_to_html.xsl'
  );
  try {
    $proc = new XsltProcessor();
  } catch (Exception $e) {
    drupal_set_message(t('@e', array('@e' => check_plain($e->getMessage()))), 'error');
    return " ";
  }

  $xsl = new DomDocument();
  $xsl->load($xslts[$input_type]);
  $input = new DomDocument();
  $input->loadXML(trim($xml_in));
  $xsl = $proc->importStylesheet($xsl);
  $newdom = $proc->transformToDoc($input);
  $return_xml = $newdom->saveXML();

  return $return_xml;
}

/**
 * Gets individual annotation urns from solr
 * called with emic/get_urns
 * @global url $base_url
 * @param <type> $pid
 */
function islandora_shared_get_annotation_urns($pid) {

  $pid = str_replace(':', '\:', $pid);
  $solr = variable_get('islandora_solr_search_block_url', 'http://localhost:8080/solr');

  $url = "http://$solr/select?indent=on&version=2.2&q=constrains_canvas%3A$pid&fq=&start=0&rows=10&fl=annotation_urn&qt=standard&wt=json&explainOther=&hl.fl=";
  $results = json_decode(file_get_contents($url), TRUE);
  $candidates = $results['response']['docs'];
  foreach ($candidates as $candidate) {
    $urns[] = $candidate['annotation_urn'][0];
  }
  $json = json_encode($urns);
  echo $json;
}

/**
 * Retrieves details of individual annotation from datastream
 * @global url $base_url
 * @param string $pid
 * @param urn $urn
 * @return IslandoraRdfBuilder
 */
function get_annotation_data($urn) {
  module_load_include('inc', 'fedora_repository', 'api/fedora_item');
  global $base_url;
  $annotation_pid = emic_canvas_get_pid_from_urn($urn);
  $item = new Fedora_Item($annotation_pid);
  if (!$item->exists()) {
    return;
  }
  $annotation_xml = $item->get_datastream_dissemination('OAC');
  return $annotation_xml;
}

/**
 * Retrieves transforms annotation data for the editor
 * Called with /emic/get_annotation/
 * Doctype declarations are removed
 * @param string $pid
 * @param urn $urn
 */
function islandora_shared_get_annotation($urn) {

  $xml = get_annotation_data($urn);
  $results = emic_shared_transform_annotationList($xml, 'rdf');
  $results = preg_replace('/<\?xml.*?\?>/', '', $results);
  $results = preg_replace('/<!DOCTYPE.*?>/', '', $results);

  echo $results;
}

/**
 *

 * @param string $pid
 * @param urn $urn
 */
function islandora_delete_annotation($pid, $urn) {
  module_load_include('inc', 'fedora_repository', 'api/fedora_item');
  $pid_to_delete = emic_canvas_get_pid_from_urn($urn);
  $item = islandora_object_load($pid);
  $item->repository->purgeObject($item->id);
}

/**
 * Persists new annotations
 * @global url $base_url
 * @param <type> $pid
 * @param <type> $rdf
 */
function islandora_save_updated_annotations($pid, $rdf) {
  global $base_url;
  module_load_include('inc', 'fedora_repository', 'api/fedora_item');
  $item = islandora_object_load($pid);
  $updated_annotationList = $rdf->printRDF();
  //$flat_pid = str_replace(':', '_', $pid);
  //$file_path = file_directory_path() . "/$flat_pid";
  //$file = "$file_path/annotationList.xml";
  //if (!is_dir($file_path)) {
  //  mkdir($file_path, 0777, TRUE);
  //}
  //file_put_contents($file, $updated_annotationList);
  $item->add_or_modify_by_reference("$base_url/$file", 'AnnotationList', "AnnotationList", 'text/plain', FALSE, 'Modified by Islandora API');
  $item['AnnotationList']->content = $updated_annotationList;
  //file_delete("$file");
  //rmdir($file_path);
}

function build_annotationList($pid) {
  global $base_url;
  $doc_url = "$base_url/islandora/object/$pid";
  $doc_path_url = "$base_url/emic/serve/$pid";
  // module_load_include('inc', 'fedora_repository', 'api/fedora_item');
  module_load_include('inc', 'emic_canvas', 'build_sharedcanvas_streams');
  module_load_include('inc', 'emic_canvas', 'islandora_rdf_builder');
  $item = islandora_object_load($pid);

  $annotationList = get_annotationList($doc_url, $doc_path_url);
  $domDoc = new DOMDocument();
  $domDoc->formatOutput = true;
  $domDoc->loadXML($annotationList);
  $rdf = new IslandoraRdfBuilder($domDoc);

  if (isset($item['AnnotationList'])) {
    $list = $item['AnnotationList']->content;
    if (strlen($list) < 50) {
      return;
    }
    $annoDoc = new DOMDocument();
    $annoDoc->loadXML($list);
    $descriptions = $annoDoc->getElementsByTagName('Description');
    foreach ($descriptions as $description) {
      $constrains = $description->getElementsByTagName('constrains');
      foreach ($constrains as $candidate) {
        $candidate->setAttribute("rdf:resource", "$base_url/fedora/repository/$pid/Canvas");
      }
      $newNode = $domDoc->importNode($description, TRUE);
      $rdf->root->appendChild($newNode);
    }
  }

  return $rdf;
}

/**
 * Gets the pid of an annotation object identified by a know urn
 * @param URN $urn
 * @return PID
 */
function emic_canvas_get_pid_from_urn($urn) {
  $solr = variable_get('islandora_solr_search_block_url', 'http://localhost:8080/solr');
  $urn = str_replace(':', '\:', $urn);
  $url = "http://$solr/select?indent=on&version=2.2&q=annotation_urn%3A$urn&fq=&start=0&rows=10&fl=PID&qt=standard&wt=json&explainOther=&hl.fl=";
  $results = json_decode(file_get_contents($url), TRUE);
  $annotation_pid = $results['response']['docs'][0]['PID'];
  return $annotation_pid;
}

/**
 * Updates existing annotation
 * Called by Shared Canvas with ajax
 */
function emic_canvas_update_annotation() {
  module_load_include('inc', 'fedora_repository', 'api/fedora_item');
  $changed = FALSE;
  //retrieve variables from POST
  $urn = urldecode($_POST['urn']);
  $title = urldecode($_POST['title']);
  $content = urldecode($_POST['content']);
  $type = urldecode($_POST['t']);
  $pid = emic_canvas_get_pid_from_urn($urn);

  $item = islandora_object_load($pid);
  $annotation = $item->get_datastream_dissemination('OAC');
  $xml = simplexml_load_string($annotation);

  // identify Title element and replace content
  $title_node = $xml->xpath("//rdf:Description[@rdf:about = 'urn:uuid:$urn']/dc:title");
  if ($title_node[0][0] != $title) {
    $title_node[0][0] = $title;
    $changed = TRUE;
  }

  //indentify URN of body to retrieve chars node to hold new annotation content
  $body_urn_array = $xml->xpath("//oac:hasBody");
  $body_urn = (string) $body_urn_array[0]->attributes('rdf', 'about');
  $content_node = $xml->xpath("//rdf:Description[@rdf:about = '$body_urn']//cnt:chars");
  if ((string) $content_node[0][0] != $content) {
    $content_node[0][0] = $content;
    $changed = TRUE;
  }


  // persist updated annotation
  if ($changed) {
    $item['OAC']->content = $xml->asXML();
    //$item->modify_datastream($xml->asXML(), 'OAC', 'OAC', 'text/plain', FALSE);
  }
}