# Guia prático: conflitos, pull/push e recuperação de commits no GitHub Desktop

Este guia reúne, em um único lugar, tudo o que foi discutido sobre:

* conflitos de merge
* diferença entre *pull* e *push*
* autenticação no GitHub
* como voltar no tempo (checkout de commits)
* como recuperar commits que “sumiram”

O foco é **projeto pessoal / hobby**, usando principalmente **GitHub Desktop**, sem Git avançado.

---

## 1. Pull x Push (regra de sobrevivência)

* **Pull** → traz mudanças do GitHub para o seu computador
* **Push** → envia suas mudanças locais para o GitHub

> Regra de ouro:
>
> * Se você mexeu no código local → **Push**
> * Se você mexeu no código pelo site do GitHub → **Pull**

⚠️ Muitos conflitos acontecem porque se faz *pull* quando o correto era *push*.

---

## 2. Por que surgem conflitos?

Conflitos aparecem quando:

* O mesmo arquivo foi alterado localmente **e** no GitHub
* O Git não sabe qual versão manter

Se **o código local está correto**, a resolução é simples:

* sempre escolher a **versão local**

No GitHub Desktop isso aparece como:

* **Use current change** (ou *Use mine*)

---

Resolver conflito escolhendo o local

Para cada arquivo com conflito:

Clique no arquivo

O GitHub Desktop vai mostrar algo parecido com:

<<<<<<< HEAD
código local
=======
código do GitHub
>>>>>>> origin/main

## 3. Resolvendo conflitos no GitHub Desktop

1. Clique em **View conflicts**
2. Para cada arquivo:

   * escolha **Use current change**
3. Marque como resolvido
4. Faça o **commit** da resolução
5. Clique em **Push origin**

Isso garante que o GitHub fique igual ao seu código local.

---

## 4. Problema de autenticação (senha não funciona)

Mensagem comum:

```
remote: Invalid username or token. Password authentication is not supported
```

Significa:

* O GitHub **não aceita mais senha** para Git
* É obrigatório usar **token** ou **GitHub Desktop autenticado**

### Solução recomendada

* Usar apenas o **GitHub Desktop**
* Fazer logout e login novamente em:

  * `File → Options → Accounts`

O Desktop renova o token automaticamente.

---

## 5. Voltar para um commit anterior (máquina do tempo)

No **History** do GitHub Desktop existem três opções importantes:

### Checkout commit ✅

* Coloca o projeto exatamente como estava naquele commit
* Não apaga nada
* Serve para navegar e testar versões antigas

👉 **É a opção correta para “voltar no tempo”**

---

### Revert Changes in commit ❌

* Cria um novo commit desfazendo apenas aquele commit específico
* Mantém o restante da linha do tempo

👉 Não serve para voltar o projeto inteiro

---

### Reorder commit ❌

* Reorganiza a ordem dos commits (rebase)
* Não muda o resultado final

👉 Não usar em projetos simples

---

### Esse commit que fiz o checkout esta errado... como voltar ao anterior? Checkout de novo?

Volte ao main em "Current branch" 

Se não funcionar:

Opção 1 — Pelo GitHub Desktop (mais simples)

a. Vá em History
b. Selecione o commit logo acima (mais antigo)
c. Clique em Checkout commit

Pronto. Agora você está nesse commit anterior.

Opção 2 — Voltar para a branch normal

- Se quiser “sair” desse modo de navegação:

Vá em Branch → Switch to branch → main (ou a branch que você usa)

Depois:

Escolha outro commit

Checkout de novo

## 6. Checkout errado? Pode trocar quantas vezes quiser

Enquanto estiver usando **Checkout commit**:

* você pode mudar de commit quantas vezes quiser
* nada é apagado
* nada é definitivo

Basta:

1. Abrir **History**
2. Escolher outro commit
3. **Checkout commit** novamente

---

## 7. Commit “sumiu” depois do checkout (detached HEAD)

Quando você faz checkout de um commit:

* o Git entra em modo **detached HEAD**
* alguns commits “posteriores” parecem desaparecer

👉 Eles **não foram apagados**, só perderam o ponteiro visual.

### Solução mais simples

No GitHub Desktop:

* **Branch → Switch to branch → main** (ou master)

Na maioria dos casos, o commit volta a aparecer no histórico.

---

## 8. Se o commit não voltar: usando o reflog

O Git mantém um registro interno de tudo que aconteceu.

No terminal:

```bash
git reflog
```

Exemplo de saída:

```
6b3dc8b HEAD@{0}: checkout: moving from main to 6b3dc8b
a91f2de HEAD@{1}: commit: ajuste reservas
f4c112a HEAD@{2}: commit: versão funcional
```

O commit “sumido” geralmente aparece aí.

Para voltar para ele:

```bash
git checkout a91f2de
```

---

## 9. Passo CRÍTICO: criar branch para não perder o commit

Assim que encontrar o commit correto:

No GitHub Desktop:

* **Branch → Create branch from this commit**

Ou no terminal:

```bash
git branch recuperacao-codigo
```

Isso garante que o commit **nunca mais desapareça**.

---

## 10. Fluxo seguro para projetos pessoais

Checklist antes de clicar em qualquer coisa:

1. Veja se existe:

   * “X commits to push”
2. Se existir → **Push primeiro**
3. Só faça pull se tiver certeza
4. Conflito apareceu?

   * pare
   * resolva conscientemente

---

## 11. Regra final (vale ouro)

> Checkout = voltar no tempo
> Branch = salvar esse ponto no tempo
> Reset/force push = só com absoluta certeza

Para projetos solo:

* GitHub Desktop resolve 99% dos problemas
* Terminal só quando você entende exatamente o comando

---
