<?php

/**
 * @file
 * Pydio callback stuff.
 */
// Redefining the CLAMAV constants as we only have an soft (optional dependancy on CLAMAV).
// If the module is not installed we will still want to say the file was unchecked.
// The file was not checked
define('ISLANDORA_SCANRESULT_UNCHECKED', -1);
// The file was checked and found to be clean.
define('ISLANDORA_SCANRESULT_CLEAN', 0);
// The file was checked and found to be infected.
define('ISLANDORA_SCANRESULT_INFECTED', 1);

/**
 * The callback proper.
 *
 * A few $_GET parameters we care about:
 * - is_dir: Whether the file to we're ingesting is actually a direcotry of
 *   files.
 * - download_base_url: Where to hit Pydio.
 * - download_query_params: URL-encoded parameters to throw back at Pydio
 *   when we go to download whatever resource. We'll parse 'em into an
 *   associative array so they're easier to use later.
 *
 * @return array
 *   A form, 'cause forms are awesome.
 */
function islandora_pydio_bridge_ingest() {
  $params = drupal_get_query_parameters();

  // Parse download_query_params into an associative array.
  if (!is_array($params['download_query_params'])) {
    // Assumes file is always the last parameter in the pydio URL
    // But allows for & in the filename which breaks normal parsing.
    $tmp_file_path = explode('&file=', $params['download_query_params'], 2)[1];
    $qp = array();
    parse_str($params['download_query_params'], $qp);
    $params['download_query_params'] = $qp;
    // Override the file so we can handle filenames with &.
    $params['download_query_params']['file'] = $tmp_file_path;
  }
  // Check to see if a folder is trying to update and treat it as a zip.
  if ($params['is_dir'] == 'true') {
    $file_path = $params['download_query_params']['file'] . '.zip';
  }
  else {
    $file_path = $params['download_query_params']['file'];
  }
  $pids = islandora_pydio_bridge_object_check($file_path, $params['repo_id']);
  if ($pids && empty($params['pid_list'])) {
    $params['pid_list'] = $pids;
    return drupal_get_form('islandora_pydio_bridge_update_form', $params);
  }
  else {
    return drupal_get_form('islandora_pydio_bridge_ingest_form', $params);
  }

}

/**
 * Form building function.
 */
function islandora_pydio_bridge_ingest_form($form, &$form_state, $params) {
  form_load_include($form_state, 'inc', 'islandora_pydio_bridge', 'includes/pydio_ingest');

  $download_url = url($params['download_base_url'], array(
    'query' => $params['download_query_params'],
    'absolute' => TRUE,
  ));

  $form_state['storage']['download_url'] = $download_url;
  $form_state['storage']['pydio_params'] = $params;

  $form_name = variable_get('islandora_pydio_bridge_ddi_form_name', 'Islandora Pydio Bridge DDI form');
  $form = xml_form_builder_get_form($form, $form_state, $form_name);

  $form['restrict_options'] = array(
    '#type' => 'select',
    '#title' => t("Is this dataset restricted?"),
    '#default_value' => 'Public',
    '#description' => t("<strong>Public</strong> - Public users can access description and download/view the dataset - a DOI is created.</br>
      <strong>Embargoed</strong> - Public users can access the description, but need to contact researcher for dataset view/download - a DOI is created.</br>
      <strong>Archive</strong> - Only Researcher/Admin can view/access - no DOI created."),
    '#options' => array(
      'Public' => t("Public"),
      'Embargoed' => t('Embargoed'),
      'Archive' => t("Archive"),
    ),
  );

  $form['embargo_duration'] = array(
    '#type' => 'radios',
    '#tree' => TRUE,
    '#title' => t('Duration of Embargo'),
    '#default_value' => 'indefinite',
    '#options' => array(
      'indefinite' => t('Embargo indefinitely (must be lifted manually)'),
      'finite' => t('Embargo until date specified'),
    ),
    '#states' => array(
      'visible' => array(
        ':input[name=restrict_options]' => array('value' => 'Embargoed'),
      ),
    ),
    '#default_value' => 'indefinite',
  );
  $form['embargo_date'] = array(
    '#type' => 'date_popup',
    '#timepicker' => FALSE,
    '#date_format' => 'Y-m-d',
    '#title' => t('Embargo date'),
    '#description' => t('The date until which this item should be embargoed (we will check with you on this date to confirm that it is ready for release)'),
    '#states' => array(
      'visible' => array(
        ':input[name=restrict_options]' => array('value' => 'Embargoed'),
        ':input[name=embargo_duration]' => array('value' => 'finite'),
      ),
    ),
    '#default_value' => FALSE,
  );

  // Set a specific callback for the submit button.
  $form['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Submit'),
  );

  if ($params['is_dir'] == 'true') {
    islandora_pydio_bridge_folder_ingest_form($form, $form_state, $params);
  }

  return $form;
}

/**
 * Hide the title of object ingest form.
 *
 * @param array $params
 *   The parameters that pydio passed.
 */
function islandora_pydio_bridge_folder_ingest_form(&$form, &$form_state, $params) {
  $file = $params['download_query_params']['file'];
  $path = explode('/', $file);
  $name = end($path);
  $form['titleInfo']['title']['#type'] = 'hidden';
  $form['titleInfo']['title']['#value'] = $name;
}

/**
 * Form validation function.
 */
function islandora_pydio_bridge_ingest_form_validate(&$form, &$form_state) {
  // TODO: Do the XML Form validation stuffs...
  $date = $form_state['values']['embargo_date'];
  $time_now = time();
  $parsed = strtotime($date);
  if ($form_state['values']['restrict_options'] == 'Embargoed' && $form_state['values']['embargo_duration'] == 'finite') {
    if ($parsed <= $time_now) {
      form_set_error('embargo_date', t('You cannot specify a date in the past!'));
      $newembargo = array();
      foreach ($form['embargo'] as $key => $value) {
        $newembargo[$key] = $value;
        if ($key == 'embargo_date') {
          $newembargo['error_message'] = array(
            "#markup" => t('You cannot specify a date in the past!'),
          );
        }
      }
      $form['embargo'] = $newembargo;
    }
  }
}

/**
 * Form submission function.
 */
function islandora_pydio_bridge_ingest_form_submit(&$form, &$form_state) {

  // Get the pydio params.
  $params = $form_state['storage']['pydio_params'];
  //Populate the hidden form element (if it exists) so we get this in the DDI
  if (isset($form_state['values']['StudyUnit']['Archive']['ArchiveSpecific']['DefaultAccess']['AccessConditions']['AccessLevel'])) {
    $form_state['values']['StudyUnit']['Archive']['ArchiveSpecific']['DefaultAccess']['AccessConditions']['AccessLevel'] = $form_state['values']['restrict_options'];
  }
  $download_url = "";
  if (isset($form_state['storage']['download_url'])) {
    $download_url = $form_state['storage']['download_url'];
  }
  else {
    drupal_set_message(t("Download Url not exist"), "error");
  }

  // Set up a batch operation.
  $batch = array(
    'operations' => array(
      array(
        'islandora_pydio_bridge_download_file_data',
        array(
          $params,
          $download_url
        )
      ),
      array(
        'islandora_pydio_bridge_ingest_object_batch',
        array($form, $form_state)
      ),
    ),
    'title' => t('Creating Dataset'),
    'init_message' => t('Ingest from DataSync'),
    'progress_message' => t('Time elapsed: @elapsed <br/>Estimated time remaning @estimate.'),
    'error_message' => t('An error has occurred.'),
    'file' => drupal_get_path('module', 'islandora_pydio_bridge') . '/includes/pydio_ingest.inc',
    'finished' => 'islandora_pydio_bridge_ingest_object_finished',
  );
  batch_set($batch);
  batch_process(ISLANDORA_PYDIO_BRIDGE_PYDIO_FRAME);
}

/**
 * Form update.
 */
function islandora_pydio_bridge_update_form($form, &$form_state, $params) {
  form_load_include($form_state, 'inc', 'islandora_pydio_bridge', 'includes/pydio_ingest');

  drupal_set_title(t("Dataset Update"));

  $download_url = url($params['download_base_url'], array(
    'query' => $params['download_query_params'],
    'absolute' => TRUE,
  ));

  $form_state['storage']['download_url'] = $download_url;
  $form_state['storage']['pydio_params'] = $params;

  $header = array(
    'pid' => t('Pid'),
    'label' => t('Label'),
  );

  foreach ($params['pid_list'] as $object) {
    $rows[] = array(
      'pid' => $object['pid']['value'],
      'label' => l($object['label']['value'], 'islandora/object/' . $object['pid']['value']),
    );
  }
  $form['current_object'] = array(
    '#type' => 'fieldset',
    '#title' => t('Research data exists in the repository with the same file path and ID number. Do you want to update an existing Dataset?'),
    '#description' => t("To create a new Dataset click Create.  To update existing Datasets select the Datasets to update, enter a reason for updating and click Update."),
  );

  $form['current_object']['pid_table'] = array(
    '#header' => $header,
    '#type' => 'tableselect',
    '#options' => $rows,
    '#empty' => t('No results found.'),
    '#js_select' => FALSE,
  );

  $form['current_object']['update_reason'] = array(
    '#type' => 'textfield',
    '#title' => t('Reason for update'),
    '#description' => t('Please enter the reason this dataset is being updated'),
    '#states' => array(
      'invisible' => array(
        ':input[name^="pid_table"]' => array('checked' => FALSE),
      ),
    ),
  );

  // Set a specific callback for the submit button.
  $form['submit_update'] = array(
    '#type' => 'submit',
    '#value' => t('Update'),
    '#states' => array(
      'invisible' => array(
        ':input[name^="pid_table"]' => array('checked' => FALSE),
      ),
    ),
  );
  $form['submit_create'] = array(
    '#type' => 'submit',
    '#value' => t('Create'),
    '#states' => array(
      'invisible' => array(
        ':input[name^="pid_table"]' => array('checked' => TRUE),
      ),
    ),
  );

  return $form;
}

/**
 * Form submission function.
 */
function islandora_pydio_bridge_update_form_submit(&$form, &$form_state) {

  $update_list = array();

  foreach ($form_state['values']['pid_table'] as $checkbox => $value) {
    if ($value !== 0) {
      $update_list[] = ($form['current_object']['pid_table']['#options'][$checkbox]);
    }
  }

  // If the list is empty then go to ingest form.
  if (!$update_list) {
    $url = url("islandora_pydio_bridge/ingest/", array(
      'absolute' => TRUE,
      'query' => $form_state['storage']['pydio_params']
    ));
    drupal_goto($url);
  }
  // Get the pydio params.
  $params = $form_state['storage']['pydio_params'];
  $download_url = "";
  if (isset($form_state['storage']['download_url'])) {
    $download_url = $form_state['storage']['download_url'];
  }
  else {
    drupal_set_message(t("Download Url not exist"), "error");
  }

  // Set up a batch operation.
  $batch = array(
    'operations' => array(
      array(
        'islandora_pydio_bridge_download_file_data',
        array(
          $params,
          $download_url
        )
      ),
      array(
        'islandora_pydio_bridge_update_object_batch',
        array($form, $form_state, $update_list)
      ),
    ),
    'title' => t('Update Datasets'),
    'init_message' => t('Updating Datasets'),
    'progress_message' => t('Time elapsed: @elapsed <br/>Estimated time remaning @estimate.'),
    'error_message' => t('An error has occurred.'),
    'file' => drupal_get_path('module', 'islandora_pydio_bridge') . '/includes/pydio_ingest.inc',
    'finished' => 'islandora_pydio_bridge_update_object_finished',
  );
  batch_set($batch);
  batch_process(ISLANDORA_PYDIO_BRIDGE_PYDIO_FRAME);
}

/**
 * The batch process of ingest object.
 */
function islandora_pydio_bridge_ingest_object_batch($form, $form_state, &$context) {
  module_load_include('inc', 'islandora_pydio_bridge', 'includes/datastream');
  module_load_include('inc', 'islandora_pydio_bridge', 'includes/utilities');
  $files = $context['results']['files'];
  $result = &$context['results'];
  $sandbox = &$context['sandbox'];
  if (!isset($sandbox['offset'])) {
    $sandbox['offset'] = 0;
    $sandbox['total'] = count($files);
    if ($sandbox['total'] === 0) {
      return;
    }
  }
  $context['message'] = t('Processing results @start to @end.', array(
    '@start' => $sandbox['offset'],
    '@end' => $sandbox['total'],
  ));
  $form_state['storage']['pydio_params']['is_dir'] = "false";
  $offset = $sandbox['offset'];

  $pid = islandora_pydio_bridge_ingest_single_object($form, $form_state, $files[$offset], $result);
  $result['islandora_batch_ingest_object'][$files[$offset]->pydio_file_path] = $pid;
  $sandbox['offset']++;

  if ($sandbox['offset'] >= $sandbox['total']) {
    file_unmanaged_delete($context['results']['files']);
    file_unmanaged_delete_recursive($context['results']['temp_zip']);
  }
  $context['finished'] = $sandbox['offset'] / $sandbox['total'];
}

/**
 * Batch finish function.
 */
function islandora_pydio_bridge_ingest_object_finished($success, $results, $operations) {
  if ($success) {
    global $user;
    $pydio_data_collection = empty($user->data['islandora_pydio_bridge_person_data_collection']) ?
      variable_get('islandora_pydio_bridge_repository_pid', 'islandora:pydio_data_top') :
      $user->data['islandora_pydio_bridge_person_data_collection'];
    drupal_goto("islandora/object/$pydio_data_collection");
  }
}

/**
 * The batch process of ingest object.
 */
function islandora_pydio_bridge_update_object_batch($form, $form_state, $update_list, &$context) {
  module_load_include('inc', 'islandora_pydio_bridge', 'includes/datastream');
  module_load_include('inc', 'islandora_pydio_bridge', 'includes/utilities');
  $file = $context['results']['files'][0];
  $result = &$context['results'];
  $sandbox = &$context['sandbox'];
  if (!isset($sandbox['offset'])) {
    $sandbox['offset'] = 0;
    $sandbox['total'] = count($update_list);
    if ($sandbox['total'] === 0) {
      return;
    }
  }
  $context['message'] = t('Processing results @start to @end.', array(
    '@start' => $sandbox['offset'],
    '@end' => $sandbox['total'],
  ));
  $form_state['storage']['pydio_params']['is_dir'] = "false";
  $offset = $sandbox['offset'];

  islandora_pydio_bridge_update_obj_datastream($form, $form_state, $file, $update_list[$offset]);
  islandora_pydio_bridge_add_new_version_event($form_state, $update_list[$offset]);
  $sandbox['offset']++;

  if ($sandbox['offset'] >= $sandbox['total']) {
    file_unmanaged_delete($context['results']['files']);
    file_unmanaged_delete_recursive($context['results']['temp_zip']);
  }
  $context['finished'] = $sandbox['offset'] / $sandbox['total'];
}

/**
 * Batch finish function.
 */
function islandora_pydio_bridge_update_object_finished($success, $results, $operations) {
  if ($success) {
    global $user;
    $pydio_data_collection = empty($user->data['islandora_pydio_bridge_person_data_collection']) ?
      variable_get('islandora_pydio_bridge_repository_pid', 'islandora:pydio_data_top') :
      $user->data['islandora_pydio_bridge_person_data_collection'];
    // TODO fix this
    drupal_goto("islandora/object/$pydio_data_collection");
  }
}

/**
 * Ingest a single object into fedora.
 *
 * @param array $form
 *   The form.
 * @param string $form_state
 *   The form state.
 * @param string $file_info
 *   Information on the file. ex file paths, file name, parent collection etc.
 *
 * @return string
 *   The new object pid.
 */
function islandora_pydio_bridge_ingest_single_object(&$form, &$form_state, $file_info, $result) {
  global $user;
  module_load_include('inc', 'islandora', 'includes/utilities');
  $parent_collection = empty($user->data['islandora_pydio_bridge_person_data_collection']) ?
    variable_get('islandora_pydio_bridge_repository_pid', 'islandora:pydio_data_top') :
    $user->data['islandora_pydio_bridge_person_data_collection'];

  // If the parent collection is not islandora_pydio_bridge root or the users own collection then find the pid of the
  // parent collection.
  if (isset($file_info->parent_collection) && ($file_info->parent_collection != $parent_collection)) {
    $parent_collection = $result['islandora_batch_ingest_object'][$file_info->parent_collection];
  }

  // Depending if the the batch is a single file or folder, the title will be
  // either the form title or the file name.
  $title = isset($file_info->file_name) ? $file_info->file_name : $form_state['values']['StudyUnit']['Citation']['Title']['Title-String'];

  // Set the file name to be the file info file name or the pydio
  // file path.
  $file_info->file_name = isset($file_info->file_name) ? $file_info->file_name : $file_info->pydio_file_path;
  $content_model = isset($file_info->content_model) ? $file_info->content_model : islandora_pydio_bridge_get_content_model($file_info->file_name);
  $new_object = islandora_prepare_new_object(variable_get("islandora_pydio_bridge_pid_namespace", "pydio"), $title, array(), array($content_model), array(
    array(
      'relationship' => 'isMemberOfCollection',
      'pid' => $parent_collection
    )
  ));
  // If the object is a file then add the OBJ and DDI datastream.
  if (isset($file_info->is_file) && $file_info->is_file == TRUE) {
    // Create OBJ datastream.
    islandora_pydio_bridge_ingest_create_obj_datastream($form, $form_state, $new_object, $file_info);
    // Create the DDI datastream.
    islandora_pydio_bridge_ingest_create_ddi_datastream($form, $form_state, $new_object);
  }

  if ($content_model == 'islandora:collectionCModel') {
    islandora_pydio_bridge_ingest_create_policy_datastream($new_object, $parent_collection);
  }
  // Add embargo to this object.
  $embargo_date = "indefinite";
  if (isset($form_state['values']['restrict_options']) &&
    $form_state['values']['restrict_options'] == 'Embargoed'
  ) {
    if (isset($form_state['values']['embargo_date']) &&
      isset($form_state['values']['embargo_duration']) &&
      $form_state['values']['embargo_duration'] == 'finite'
    ) {
      $embargo_date = $form_state['values']['embargo_date'];
    }
  }

  islandora_pydio_bridge_ingest_set_xacml($new_object, $form_state['values']['restrict_options'], $embargo_date);
  islandora_pydio_bridge_ingest_create_doi($new_object, $form_state['values']['restrict_options']);

  // Add the pydio file path and repository id to RELS-EXT
  $repo_id = $form_state['storage']['pydio_params']['repo_id'];
  islandora_pydio_bridge_add_file_object_relationship($file_info->pydio_file_path, $repo_id, $new_object);
  // Add object.
  islandora_add_object($new_object);
  // Run the xslt transforms
  module_load_include('inc', 'xml_forms', 'builder/includes/associations');
  $transform = islandora_pydio_bridge_xml_form_builder_get_transforms();
  // Transform the DDI to DC
  $updated_dc = xml_form_builder_transform_document(DomDocument::load(drupal_realpath($transform['ddi_to_dc.xsl'])), DOMDocument::loadXML($new_object['DDI']->content));
  if (!empty($updated_dc)) {
    $new_object['DC']->setContentFromString($updated_dc->saveXML());
  }
  // Return the new object pid.
  return $new_object->id;
}

/**
 * Get the data from a file using a url.
 *
 * @param AbstractObject|string $download_url
 *   The url of the file to load.
 *
 * @returns string $temp_file_path
 *   The path of the file in the tmp directory.
 */
function islandora_pydio_bridge_download_file_data($params, $download_url, &$context) {
  $context['message'] = t('Downloading Content.');
  // Get the path info of the download_url
  $download_url_path_info = pathinfo($download_url);
  // Check to see if the file is a zip file.
  $is_zip_file = isset($download_url_path_info['extension']) ? FALSE : TRUE;
  // We want to pass the cookie information.
  $temparray = array();
  foreach ($_COOKIE as $key => $value) {
    $temparray[] = $key . "=" . $value;
  }
  $temparray[] = 'path:/';
  $cookies = implode('; ', $temparray);

  $temp_file_name = 'pydio_temp';
  $temp_file_path = file_create_filename($temp_file_name, 'temporary://');
  $fp = fopen($temp_file_path, 'w+');
  $ch = curl_init($download_url);
  curl_setopt($ch, CURLOPT_COOKIE, $cookies);
  curl_setopt($ch, CURLOPT_RETURNTRANSFER, 1);
  //large files were timing out, setting at 10minutes for now, we may have to set this to
  // 0 if we want to support files of any size.
  curl_setopt($ch, CURLOPT_TIMEOUT, 600);
  curl_setopt($ch, CURLOPT_FILE, $fp);
  curl_setopt($ch, CURLOPT_FOLLOWLOCATION, TRUE);
  curl_exec($ch);
  if (curl_errno($ch)) {
    $curl_error = curl_error($ch);
    watchdog("islandora_pydio_bridge", "Failed to download file from %url, %message", array(
      "%url" => $download_url,
      "%message" => $curl_error,
    ), WATCHDOG_ERROR, NULL);
  }

  curl_close($ch);
  fclose($fp);

  if ($is_zip_file) {
    // Add a zip extension to the filename and treat it as a single file
    $params['download_query_params']['file'] = $params['download_query_params']['file'] . '.zip';
    //islandora_pydio_bridge_get_folder_data($temp_file_path, $context);
    islandora_pydio_bridge_get_file_data($params, $temp_file_path, $context);
  }
  else {
    islandora_pydio_bridge_get_file_data($params, $temp_file_path, $context);
  }
}

/**
 * Get the file information.
 *
 * @param array $params
 *   The pydio parameters.
 * @param string $file_path
 *   The download url of file.
 */
function islandora_pydio_bridge_get_file_data($params, $file_path, &$context) {
  global $user;
  $file = drupal_realpath($file_path);

  $root_collection = empty($user->data['islandora_pydio_bridge_person_data_collection']) ?
    variable_get('islandora_pydio_bridge_repository_pid', 'islandora:pydio_data_top') :
    $user->data['islandora_pydio_bridge_person_data_collection'];

  $files_info = array();
  $file_entry = new stdClass();
  $file_entry->parent_collection = $root_collection;
  $file_entry->pydio_file_path = $params['download_query_params']['file'];
  $file_entry->is_file = TRUE;
  $file_entry->uri = $file;
  $files_info[] = $file_entry;

  $context['results']['files'] = $files_info;
  $context['results']['temp_zip'] = $file;
}

/**
 * Add relationship between pydio file and islandora object.
 *
 * @param string $file_path
 *   The path of the file in pydio.
 * @param string $repo_id
 *   The repository Id of the file on pydio.
 * @param FedoraObject $object
 *   The object that should add relationship
 */
function islandora_pydio_bridge_add_file_object_relationship($file_path, $repo_id, $object) {

  $object->relationships->registerNamespace("ajxp", ISLANDORA_PYDIO_BRIDGE_PYDIO_URI);
  $object->relationships->add(ISLANDORA_PYDIO_BRIDGE_PYDIO_URI, "file", $file_path, TRUE);
  $object->relationships->add(ISLANDORA_PYDIO_BRIDGE_PYDIO_URI, "repository", $repo_id, TRUE);
}

/**
 * Check to see if the objects exists in fedora.
 *
 * @param string $file_path
 *   The path of the file in pydio.
 * @param string $repo_id
 *   The repository Id of the file on pydio.
 *
 * @return array
 *   Returns the result form the query.
 */
function islandora_pydio_bridge_object_check($file_path, $repo_id) {
  module_load_include('inc', 'islandora', 'includes/utilities');

  $repo = islandora_get_tuque_connection();

  $pydio_namespace = ISLANDORA_PYDIO_BRIDGE_PYDIO_URI;
  $query = "PREFIX pydio:<$pydio_namespace>
    SELECT ?pid ?label
    FROM <#ri>
    WHERE
    {
      ?pid pydio:file \"$file_path\" ;
      pydio:repository \"$repo_id\" ;
      <fedora-model:label> ?label
    }";

  $result = $repo->repository->ri->sparqlQuery($query);
  return $result;
}

/**
 * Function to return the content model based on mimetype.
 *
 * @param string $file
 *   The file.
 *
 * @return string
 *   Returns the content model to be used.
 */
function islandora_pydio_bridge_get_content_model($file) {
  module_load_include('inc', 'islandora', 'includes/mime_detect');

  // Get the mimetype of the file.
  $mime_detect = new MimeDetect();
  $mimetype = $mime_detect->getMimetype($file);

  // Get all of the enabled solution packs.
  $connection = islandora_get_tuque_connection();
  $enabled_solution_packs = module_invoke_all('islandora_required_objects', $connection);

  // Set generic default content model.
  $content_model = "islandora:binaryObjectCModel";

  switch ($mimetype) {
    // Basic Image.
    case "image/jpg":
    case "image/jpeg":
    case "image/png":
    case "image/gif":
      if (isset($enabled_solution_packs['islandora_basic_image'])) {
        $content_model = "islandora:sp_basic_image";
      }
      break;

    // Large Image.
    case "image/tiff":
      if (isset($enabled_solution_packs['islandora_large_image'])) {
        $content_model = "islandora:sp_large_image_cmodel";
      }
      break;

    // PDF.
    case "application/pdf":
      if (isset($enabled_solution_packs['islandora_pdf'])) {
        $content_model = "islandora:sp_pdf";
      }
      break;

    // Video.
    case "video/mp4":
    case "video/quicktime":
    case "video/m4v":
    case "video/x-matroska":
    case "video/x-msvideo":
    case "video/ogg":
      if (isset($enabled_solution_packs['islandora_video'])) {
        $content_model = "islandora:sp_videoCModel";
      }
      break;

    // Audio.
    case "audio/mpeg":
    case "audio/wav":
    case "audio/x-wav":
    case "audio/vnd.wave":
    case "audio/flac":
      if (isset($enabled_solution_packs['islandora_audio'])) {
        $content_model = "islandora:sp-audioCModel";
      }
      break;

    // Tabular Data.
    case "text/csv":
    case "application/x-spss-sav":
    case "application/vnd.ms-excel":
    case "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet":
      if (isset($enabled_solution_packs['islandora_data'])) {
        $content_model = "islandora:sp_data";
      }
      break;
  }

  return $content_model;
}
