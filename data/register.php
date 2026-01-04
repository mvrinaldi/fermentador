<?php
// register.php - Processamento do cadastro de usuário

header('Content-Type: application/json');
header('Access-Control-Allow-Origin: *');
header('Access-Control-Allow-Methods: POST');
header('Access-Control-Allow-Headers: Content-Type');

// Configurações do banco de dados
$host = 'localhost';
$dbname = 'u865276125_ferment_bd';
$username = 'u865276125_ferment_user';
$password = '6c3@5mZ8';

// Inicializar resposta
$response = ['success' => false, 'message' => ''];

try {
    // Conectar ao banco de dados
    $pdo = new PDO("mysql:host=$host;dbname=$dbname;charset=utf8mb4", $username, $password);
    $pdo->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
    
    // Ler dados do POST
    $data = json_decode(file_get_contents('php://input'), true);
    
    if (!$data) {
        throw new Exception('Dados não recebidos');
    }
    
    // Validar campos obrigatórios
    $required = ['name', 'email', 'password'];
    foreach ($required as $field) {
        if (empty($data[$field])) {
            throw new Exception("Campo $field é obrigatório");
        }
    }
    
    $name = trim($data['name']);
    $email = trim($data['email']);
    $password = $data['password'];
    
    // Validar email
    if (!filter_var($email, FILTER_VALIDATE_EMAIL)) {
        throw new Exception('Email inválido');
    }
    
    // Validar senha (mínimo 6 caracteres)
    if (strlen($password) < 6) {
        throw new Exception('A senha deve ter pelo menos 6 caracteres');
    }
    
    // Verificar se email já existe
    $stmt = $pdo->prepare("SELECT id FROM users WHERE email = ?");
    $stmt->execute([$email]);
    
    if ($stmt->rowCount() > 0) {
        throw new Exception('Este email já está cadastrado');
    }
    
    // Hash da senha
    $password_hash = password_hash($password, PASSWORD_DEFAULT);
    
    // Inserir usuário
    $stmt = $pdo->prepare("
        INSERT INTO users (email, password_hash, created_at, is_active) 
        VALUES (?, ?, NOW(), 1)
    ");
    
    $stmt->execute([$email, $password_hash]);
    $user_id = $pdo->lastInsertId();
    
    // Registrar ação no histórico
    $stmt = $pdo->prepare("
        INSERT INTO action_history (user_id, action_type, action_details, action_timestamp) 
        VALUES (?, 'user_registration', ?, NOW())
    ");
    
    $details = json_encode(['name' => $name, 'email' => $email]);
    $stmt->execute([$user_id, $details]);
    
    // Sucesso
    $response['success'] = true;
    $response['message'] = 'Usuário cadastrado com sucesso!';
    $response['user_id'] = $user_id;
    
} catch (PDOException $e) {
    // Erro do banco de dados
    $response['message'] = 'Erro no banco de dados: ' . $e->getMessage();
    error_log('Database Error: ' . $e->getMessage());
    
} catch (Exception $e) {
    // Erro geral
    $response['message'] = $e->getMessage();
}

// Retornar resposta JSON
echo json_encode($response);
?>